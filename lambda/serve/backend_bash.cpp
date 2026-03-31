/**
 * @file backend_bash.cpp
 * @brief Bash CGI backend for web server
 *
 * Runs Bash handler scripts via subprocess with CGI environment.
 * Each request spawns a shell process with standard CGI env vars.
 * Script output is parsed as CGI response (headers + body).
 *
 * Compatible with:
 *   CGI/1.1:    Standard CGI specification (RFC 3875)
 *   Apache:     ScriptAlias /cgi-bin/ /var/www/cgi-bin/
 *   Nginx:      fastcgi_pass / uwsgi_pass
 */

#include "language_backend.hpp"
#include "serve_utils.hpp"
#include "../../lib/log.h"

#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

// ============================================================================
// Bash Backend State
// ============================================================================

struct BashBackendData {
    char *shell_path;           // path to bash/sh executable
    int initialized;
};

// ============================================================================
// Backend interface
// ============================================================================

static int bash_backend_init(LanguageBackend *self) {
    BashBackendData *data = (BashBackendData*)serve_calloc(1, sizeof(BashBackendData));
    if (!data) return -1;

    const char *sh = getenv("LAMBDA_SHELL");
    data->shell_path = serve_strdup(sh ? sh : "/bin/bash");
    data->initialized = 1;
    self->backend_data = data;

    log_info("bash backend initialized (shell: %s)", data->shell_path);
    return 0;
}

static void bash_backend_shutdown(LanguageBackend *self) {
    BashBackendData *data = (BashBackendData*)self->backend_data;
    if (data) {
        serve_free(data->shell_path);
        serve_free(data);
        self->backend_data = NULL;
    }
    log_info("bash backend shut down");
}

static int bash_backend_execute(LanguageBackend *self, const char *handler_path,
                                  HttpRequest *req, HttpResponse *resp) {
    BashBackendData *data = (BashBackendData*)self->backend_data;
    if (!data || !data->initialized) return BACKEND_ERROR;

    if (!serve_file_exists(handler_path)) {
        log_error("bash handler not found: %s", handler_path);
        return BACKEND_NOT_FOUND;
    }

    // check executable permission
    struct stat st;
    if (stat(handler_path, &st) != 0 || !(st.st_mode & S_IXUSR)) {
        log_error("bash handler not executable: %s", handler_path);
        http_response_error(resp, HTTP_403_FORBIDDEN,
                           "CGI script is not executable");
        return BACKEND_ERROR;
    }

    // TODO: Phase 5 — bash_execute_cgi(handler_path, req, resp)
    // 1. Build CGI environment from request (same as python backend)
    // 2. Spawn bash subprocess: shell_path handler_path
    // 3. Pipe request body to stdin
    // 4. Read CGI output from stdout:
    //    Content-Type: text/html\r\n
    //    Status: 200 OK\r\n
    //    \r\n
    //    <html>...</html>
    // 5. Parse CGI headers and body into HttpResponse

    log_error("bash backend execute not yet implemented for: %s", handler_path);
    http_response_error(resp, HTTP_501_NOT_IMPLEMENTED,
                       "Bash CGI handler execution not yet implemented");
    return BACKEND_ERROR;
}

static int bash_backend_compile(LanguageBackend *self, const char *handler_path) {
    (void)self; (void)handler_path;
    return BACKEND_OK; // Bash is interpreted
}

static int bash_backend_needs_recompile(LanguageBackend *self, const char *handler_path) {
    (void)self; (void)handler_path;
    return 0; // Bash is interpreted
}

// ============================================================================
// Backend factory
// ============================================================================

static const char *bash_extensions[] = { ".sh", ".bash", ".cgi" };

static LanguageBackend bash_backend_instance = {
    "bash",
    bash_extensions,
    3,
    NULL,
    bash_backend_init,
    bash_backend_shutdown,
    bash_backend_execute,
    bash_backend_compile,
    bash_backend_needs_recompile
};

LanguageBackend* create_bash_backend(void) {
    return &bash_backend_instance;
}
