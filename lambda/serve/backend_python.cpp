/**
 * @file backend_python.cpp
 * @brief Python subprocess + WSGI backend for web server
 *
 * Runs Python handler scripts via persistent subprocess workers.
 * Supports WSGI protocol for Flask/Django integration.
 *
 * Architecture:
 *   - Spawns Python subprocess via uv_spawn() with CGI environment
 *   - Persistent workers for WSGI apps (reused across requests)
 *   - Request/response communicated via stdin/stdout + CGI env vars
 *
 * Compatible with:
 *   Flask:      from flask import Flask; app = Flask(__name__)
 *   Django:     django.core.handlers.wsgi.WSGIHandler
 *   Gunicorn:   gunicorn app:app (subprocess model)
 */

#include "language_backend.hpp"
#include "serve_utils.hpp"
#include "../../lib/log.h"

#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

// ============================================================================
// Python Backend State
// ============================================================================

struct PyBackendData {
    char *python_path;          // path to python3 executable
    int initialized;
};

// ============================================================================
// CGI environment builder
// ============================================================================

// build CGI-style environment variables for a request
static char** build_cgi_env(HttpRequest *req, int *out_count) {
    // CGI spec environment variables
    int max_vars = 32;
    char **env = (char**)serve_calloc((size_t)(max_vars + 1), sizeof(char*));
    int n = 0;

    // helper to add "KEY=VALUE" to env array
    #define ADD_ENV(key, val) do { \
        if (n < max_vars && (val)) { \
            size_t klen = strlen(key); \
            size_t vlen = strlen(val); \
            char *entry = (char*)serve_malloc(klen + 1 + vlen + 1); \
            memcpy(entry, key, klen); \
            entry[klen] = '='; \
            memcpy(entry + klen + 1, val, vlen + 1); \
            env[n++] = entry; \
        } \
    } while(0)

    ADD_ENV("REQUEST_METHOD", http_method_to_string(req->method));
    ADD_ENV("REQUEST_URI", req->uri);
    ADD_ENV("PATH_INFO", req->path);
    ADD_ENV("QUERY_STRING", req->query_string ? req->query_string : "");
    ADD_ENV("SERVER_PROTOCOL", "HTTP/1.1");
    ADD_ENV("GATEWAY_INTERFACE", "CGI/1.1");

    const char *ct = http_request_content_type(req);
    if (ct) ADD_ENV("CONTENT_TYPE", ct);

    if (req->body_len > 0) {
        char cl_str[32];
        snprintf(cl_str, sizeof(cl_str), "%zu", req->body_len);
        ADD_ENV("CONTENT_LENGTH", cl_str);
    }

    if (req->remote_addr) ADD_ENV("REMOTE_ADDR", req->remote_addr);

    // pass HTTP headers as HTTP_* env vars
    HttpHeader *h = req->headers;
    while (h && n < max_vars) {
        // convert header name to HTTP_UPPER_CASE
        size_t name_len = strlen(h->name);
        char *env_name = (char*)serve_malloc(5 + name_len + 1); // "HTTP_" + name + \0
        memcpy(env_name, "HTTP_", 5);
        for (size_t i = 0; i < name_len; i++) {
            char c = h->name[i];
            if (c == '-') c = '_';
            if (c >= 'a' && c <= 'z') c -= 32;
            env_name[5 + i] = c;
        }
        env_name[5 + name_len] = '\0';
        ADD_ENV(env_name, h->value);
        serve_free(env_name);
        h = h->next;
    }

    #undef ADD_ENV

    env[n] = NULL;
    *out_count = n;
    return env;
}

static void free_cgi_env(char **env, int count) {
    for (int i = 0; i < count; i++) {
        serve_free(env[i]);
    }
    serve_free(env);
}

// ============================================================================
// Backend interface
// ============================================================================

static int python_backend_init(LanguageBackend *self) {
    PyBackendData *data = (PyBackendData*)serve_calloc(1, sizeof(PyBackendData));
    if (!data) return -1;

    // find python3
    const char *py = getenv("LAMBDA_PYTHON");
    data->python_path = serve_strdup(py ? py : "python3");
    data->initialized = 1;
    self->backend_data = data;

    log_info("python backend initialized (interpreter: %s)", data->python_path);
    return 0;
}

static void python_backend_shutdown(LanguageBackend *self) {
    PyBackendData *data = (PyBackendData*)self->backend_data;
    if (data) {
        serve_free(data->python_path);
        serve_free(data);
        self->backend_data = NULL;
    }
    log_info("python backend shut down");
}

static int python_backend_execute(LanguageBackend *self, const char *handler_path,
                                    HttpRequest *req, HttpResponse *resp) {
    PyBackendData *data = (PyBackendData*)self->backend_data;
    if (!data || !data->initialized) return BACKEND_ERROR;

    if (!serve_file_exists(handler_path)) {
        log_error("python handler not found: %s", handler_path);
        return BACKEND_NOT_FOUND;
    }

    // TODO: Phase 4 — python_execute_wsgi(handler_path, req, resp)
    // 1. Build CGI environment from request
    // 2. Spawn python3 subprocess with CGI env
    // 3. Pipe request body to stdin
    // 4. Read CGI response from stdout (Status: line, headers, blank line, body)
    // 5. Parse CGI output into HttpResponse

    // For now, build CGI env as proof of concept
    int env_count = 0;
    char **cgi_env = build_cgi_env(req, &env_count);
    free_cgi_env(cgi_env, env_count);

    log_error("python backend execute not yet implemented for: %s", handler_path);
    http_response_error(resp, HTTP_501_NOT_IMPLEMENTED,
                       "Python handler execution not yet implemented");
    return BACKEND_ERROR;
}

static int python_backend_compile(LanguageBackend *self, const char *handler_path) {
    (void)self; (void)handler_path;
    return BACKEND_OK; // Python is interpreted, no compilation needed
}

static int python_backend_needs_recompile(LanguageBackend *self, const char *handler_path) {
    (void)self; (void)handler_path;
    return 0; // Python is interpreted
}

// ============================================================================
// Backend factory
// ============================================================================

static const char *python_extensions[] = { ".py" };

static LanguageBackend python_backend_instance = {
    "python",
    python_extensions,
    1,
    NULL,
    python_backend_init,
    python_backend_shutdown,
    python_backend_execute,
    python_backend_compile,
    python_backend_needs_recompile
};

LanguageBackend* create_python_backend(void) {
    return &python_backend_instance;
}
