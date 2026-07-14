#if defined(__linux__)

#include <GLFW/glfw3.h>
#include "webview_handle_linux.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "../lib/log.h"
}

void webview_linux_finish_lambda_scheme_request(WebKitURISchemeRequest* request,
                                                const char* log_prefix) {
    const char* path = webkit_uri_scheme_request_get_path(request);
    if (!path) path = "/";
    const char* relative_path = path[0] == '/' ? path + 1 : path;

    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        GError* error = g_error_new(G_FILE_ERROR, G_FILE_ERROR_NOENT, "failed to get CWD");
        webkit_uri_scheme_request_finish_error(request, error);
        g_error_free(error);
        return;
    }

    char full_path[8192];
    snprintf(full_path, sizeof(full_path), "%s/%s", cwd, relative_path);
    char* resolved = realpath(full_path, nullptr);
    if (!resolved) {
        log_error("%s: file not found: %s", log_prefix, full_path);
        GError* error = g_error_new(
            G_FILE_ERROR, G_FILE_ERROR_NOENT, "file not found: %s", full_path);
        webkit_uri_scheme_request_finish_error(request, error);
        g_error_free(error);
        return;
    }

    char* resolved_cwd = realpath(cwd, nullptr);
    if (!resolved_cwd || strncmp(resolved, resolved_cwd, strlen(resolved_cwd)) != 0) {
        log_error("%s: path traversal blocked: %s", log_prefix, resolved);
        if (resolved_cwd) g_free(resolved_cwd);
        g_free(resolved);
        GError* error = g_error_new(
            G_FILE_ERROR, G_FILE_ERROR_ACCES, "path traversal blocked");
        webkit_uri_scheme_request_finish_error(request, error);
        g_error_free(error);
        return;
    }
    g_free(resolved_cwd);

    GError* file_error = nullptr;
    GMappedFile* mapped = g_mapped_file_new(resolved, FALSE, &file_error);
    g_free(resolved);
    if (!mapped) {
        log_error("%s: cannot read file: %s", log_prefix, file_error->message);
        webkit_uri_scheme_request_finish_error(request, file_error);
        g_error_free(file_error);
        return;
    }

    gsize data_len = g_mapped_file_get_length(mapped);
    const char* data = g_mapped_file_get_contents(mapped);
    GInputStream* stream = g_memory_input_stream_new_from_data(
        g_memdup2(data, data_len), data_len, g_free);
    g_mapped_file_unref(mapped);

    const char* mime = webview_linux_mime_for_path(full_path);
    webkit_uri_scheme_request_finish(request, stream, (gint64)data_len, mime);
    g_object_unref(stream);
    log_debug("%s: served %s (%zu bytes, %s)", log_prefix, full_path, data_len, mime);
}

#endif
