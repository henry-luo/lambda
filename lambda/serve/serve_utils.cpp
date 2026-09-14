/**
 * @file serve_utils.cpp
 * @brief Utility function implementations for the Lambda web server
 *
 * Migrated from lib/serve/utils.c to C+ (.cpp).
 */

#include "serve_utils.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/file.h"
#include "../../lib/str.h"
#include "../../lib/url.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

// ============================================================================
// Memory management
// ============================================================================

void* serve_malloc(size_t size) {
    void *ptr = mem_alloc(size, MEM_CAT_SERVE);
    if (!ptr && size > 0) {
        log_error("serve_malloc: allocation failed for %zu bytes", size);
    }
    return ptr;
}

void* serve_calloc(size_t count, size_t size) {
    void *ptr = mem_calloc(count, size, MEM_CAT_SERVE);
    if (!ptr && count > 0 && size > 0) {
        log_error("serve_calloc: allocation failed for %zu * %zu bytes", count, size);
    }
    return ptr;
}

void* serve_realloc(void *ptr, size_t size) {
    void *new_ptr = mem_realloc(ptr, size, MEM_CAT_SERVE);
    if (!new_ptr && size > 0) {
        log_error("serve_realloc: reallocation failed for %zu bytes", size);
    }
    return new_ptr;
}

void serve_free(void *ptr) {
    mem_free(ptr);
}

char* serve_strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *dup = (char *)serve_malloc(len + 1);
    if (dup) {
        memcpy(dup, str, len + 1);
    }
    return dup;
}

// ============================================================================
// Error handling (thread-local error buffer)
// ============================================================================

static thread_local char error_buffer[512] = {0};

void serve_set_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(error_buffer, sizeof(error_buffer), format, args);
    va_end(args);
}

const char* serve_get_error(void) {
    return error_buffer;
}

void serve_clear_error(void) {
    error_buffer[0] = '\0';
}

// ============================================================================
// String utilities
// ============================================================================

int serve_strcasecmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return s1 != s2;
    return str_icmp(s1, strlen(s1), s2, strlen(s2));
}

char* serve_strtrim(char *str) {
    if (!str) return NULL;

    // trim leading
    while (*str && isspace((unsigned char)*str)) str++;

    // trim trailing
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }

    return str;
}

size_t serve_url_decode(char *str) {
    // application/x-www-form-urlencoded in-place decode (%XX and '+' -> ' ')
    return url_decode_inplace(str, true);
}

const char* serve_get_file_extension(const char *path) {
    const char* ext = file_path_ext(path);
    return ext ? ext : "";
}

// ============================================================================
// Time utilities
// ============================================================================

char* serve_http_date(char *buffer, size_t bufsize) {
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);
    strftime(buffer, bufsize, "%a, %d %b %Y %H:%M:%S GMT", gmt);
    return buffer;
}

char* serve_file_mtime_str(const char *filepath, char *buffer, size_t bufsize) {
    FileStat stat = file_stat(filepath);
    if (!stat.exists) {
        buffer[0] = '\0';
        return buffer;
    }
    struct tm *gmt = gmtime(&stat.modified);
    strftime(buffer, bufsize, "%a, %d %b %Y %H:%M:%S GMT", gmt);
    return buffer;
}

// ============================================================================
// File utilities
// ============================================================================

int serve_file_exists(const char *filepath) {
    return filepath && file_exists(filepath);
}

long serve_file_size(const char *filepath) {
    return filepath ? (long)file_size(filepath) : -1;
}

char* serve_read_file(const char *filepath, size_t *out_size) {
    if (out_size) *out_size = 0;
    if (!filepath) return NULL;

    char* data = NULL;
    if (!file_read_all(filepath, MEM_CAT_SERVE, &data, out_size)) {
        serve_set_error("serve_read_file: failed to read '%s'", filepath);
        return NULL;
    }
    return data;
}
