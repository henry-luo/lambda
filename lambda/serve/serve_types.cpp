/**
 * @file serve_types.cpp
 * @brief Implementation of core serve types — headers, methods, status, config
 *
 * Migrated from lib/serve/http_handler.c and lib/serve/server.c to C+ (.cpp).
 */

#include "serve_types.hpp"
#include "serve_utils.hpp"
#include <string.h>
#include <ctype.h>

// ============================================================================
// Header Linked List
// ============================================================================

HttpHeader* http_header_add(HttpHeader *list, const char *name, const char *value) {
    HttpHeader *h = (HttpHeader *)serve_calloc(1, sizeof(HttpHeader));
    if (!h) return list;
    h->name = serve_strdup(name);
    h->value = serve_strdup(value);
    h->next = list;
    return h;
}

const char* http_header_find(const HttpHeader *list, const char *name) {
    for (const HttpHeader *h = list; h; h = h->next) {
        if (h->name && name && serve_strcasecmp(h->name, name) == 0) {
            return h->value;
        }
    }
    return NULL;
}

void http_header_free(HttpHeader *list) {
    while (list) {
        HttpHeader *next = list->next;
        serve_free(list->name);
        serve_free(list->value);
        serve_free(list);
        list = next;
    }
}

HttpHeader* http_header_remove(HttpHeader *list, const char *name) {
    if (!name) return list;
    HttpHeader *prev = NULL;
    HttpHeader *curr = list;
    while (curr) {
        if (curr->name && serve_strcasecmp(curr->name, name) == 0) {
            HttpHeader *next = curr->next;
            if (prev) {
                prev->next = next;
            } else {
                list = next;
            }
            serve_free(curr->name);
            serve_free(curr->value);
            serve_free(curr);
            curr = next;
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
    return list;
}

// ============================================================================
// HTTP Method Conversion
// ============================================================================

HttpMethod http_method_from_string(const char *str) {
    if (!str) return HTTP_UNKNOWN;
    if (strcmp(str, "GET") == 0)     return HTTP_GET;
    if (strcmp(str, "POST") == 0)    return HTTP_POST;
    if (strcmp(str, "PUT") == 0)     return HTTP_PUT;
    if (strcmp(str, "DELETE") == 0)  return HTTP_DELETE;
    if (strcmp(str, "HEAD") == 0)    return HTTP_HEAD;
    if (strcmp(str, "OPTIONS") == 0) return HTTP_OPTIONS;
    if (strcmp(str, "PATCH") == 0)   return HTTP_PATCH;
    if (strcmp(str, "CONNECT") == 0) return HTTP_CONNECT;
    if (strcmp(str, "TRACE") == 0)   return HTTP_TRACE;
    return HTTP_UNKNOWN;
}

const char* http_method_to_string(HttpMethod method) {
    switch (method) {
        case HTTP_GET:     return "GET";
        case HTTP_POST:    return "POST";
        case HTTP_PUT:     return "PUT";
        case HTTP_DELETE:  return "DELETE";
        case HTTP_HEAD:    return "HEAD";
        case HTTP_OPTIONS: return "OPTIONS";
        case HTTP_PATCH:   return "PATCH";
        case HTTP_CONNECT: return "CONNECT";
        case HTTP_TRACE:   return "TRACE";
        default:           return "UNKNOWN";
    }
}

// ============================================================================
// HTTP Status Codes
// ============================================================================

const char* http_status_string(int status_code) {
    switch (status_code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 422: return "Unprocessable Entity";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default:  return "Unknown";
    }
}

// ============================================================================
// Server Configuration
// ============================================================================

ServerConfig server_config_default(void) {
    ServerConfig config;
    memset(&config, 0, sizeof(ServerConfig));
    config.port = 8080;
    config.ssl_port = 0;
    config.max_connections = 1024;
    config.timeout_seconds = 60;
    config.max_header_size = 8192;
    config.max_body_size = 10 * 1024 * 1024; // 10 MB
    config.keep_alive = 1;
    config.keep_alive_timeout = 5;
    config.max_requests_per_conn = 100;
    return config;
}

int server_config_validate(const ServerConfig *config) {
    if (!config) {
        serve_set_error("null configuration");
        return -1;
    }

    if (config->port <= 0 && config->ssl_port <= 0) {
        serve_set_error("at least one port (http or https) must be specified");
        return -1;
    }

    if (config->port > 0 && (config->port < 1 || config->port > 65535)) {
        serve_set_error("invalid http port: %d", config->port);
        return -1;
    }

    if (config->ssl_port > 0 && (config->ssl_port < 1 || config->ssl_port > 65535)) {
        serve_set_error("invalid https port: %d", config->ssl_port);
        return -1;
    }

    if (config->ssl_port > 0) {
        if (!config->ssl_cert_file || !config->ssl_key_file) {
            serve_set_error("ssl certificate and key files required for https");
            return -1;
        }
    }

    if (config->max_connections < 0) {
        serve_set_error("invalid max connections: %d", config->max_connections);
        return -1;
    }

    if (config->timeout_seconds < 0) {
        serve_set_error("invalid timeout: %d", config->timeout_seconds);
        return -1;
    }

    return 0;
}

void server_config_cleanup(ServerConfig *config) {
    if (!config) return;
    serve_free(config->bind_address);
    serve_free(config->ssl_cert_file);
    serve_free(config->ssl_key_file);
    serve_free(config->document_root);
    memset(config, 0, sizeof(ServerConfig));
}
