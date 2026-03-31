/**
 * @file serve_utils.cpp
 * @brief Utility function implementations for the Lambda web server
 *
 * Migrated from lib/serve/utils.c to C+ (.cpp).
 */

#include "serve_utils.hpp"
#include "../../lib/log.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

// ============================================================================
// Memory management
// ============================================================================

void* serve_malloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr && size > 0) {
        log_error("serve_malloc: allocation failed for %zu bytes", size);
    }
    return ptr;
}

void* serve_calloc(size_t count, size_t size) {
    void *ptr = calloc(count, size);
    if (!ptr && count > 0 && size > 0) {
        log_error("serve_calloc: allocation failed for %zu * %zu bytes", count, size);
    }
    return ptr;
}

void* serve_realloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        log_error("serve_realloc: reallocation failed for %zu bytes", size);
    }
    return new_ptr;
}

void serve_free(void *ptr) {
    free(ptr);
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

static _Thread_local char error_buffer[512] = {0};

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
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
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
    if (!str) return 0;
    char *src = str;
    char *dst = str;

    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            int hi = -1, lo = -1;
            char c1 = src[1], c2 = src[2];
            if (c1 >= '0' && c1 <= '9') hi = c1 - '0';
            else if (c1 >= 'a' && c1 <= 'f') hi = c1 - 'a' + 10;
            else if (c1 >= 'A' && c1 <= 'F') hi = c1 - 'A' + 10;
            if (c2 >= '0' && c2 <= '9') lo = c2 - '0';
            else if (c2 >= 'a' && c2 <= 'f') lo = c2 - 'a' + 10;
            else if (c2 >= 'A' && c2 <= 'F') lo = c2 - 'A' + 10;

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

const char* serve_get_file_extension(const char *path) {
    if (!path) return "";
    const char *dot = NULL;
    const char *p = path;
    while (*p) {
        if (*p == '.') dot = p;
        if (*p == '/' || *p == '\\') dot = NULL; // reset after directory separator
        p++;
    }
    return dot ? dot : "";
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
    struct stat st;
    if (stat(filepath, &st) != 0) {
        buffer[0] = '\0';
        return buffer;
    }
    struct tm *gmt = gmtime(&st.st_mtime);
    strftime(buffer, bufsize, "%a, %d %b %Y %H:%M:%S GMT", gmt);
    return buffer;
}

// ============================================================================
// File utilities
// ============================================================================

int serve_file_exists(const char *filepath) {
    if (!filepath) return 0;
    struct stat st;
    return stat(filepath, &st) == 0;
}

long serve_file_size(const char *filepath) {
    if (!filepath) return -1;
    struct stat st;
    if (stat(filepath, &st) != 0) return -1;
    return (long)st.st_size;
}

char* serve_read_file(const char *filepath, size_t *out_size) {
    if (!filepath) return NULL;

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        serve_set_error("serve_read_file: cannot open '%s'", filepath);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        serve_set_error("serve_read_file: cannot determine size of '%s'", filepath);
        return NULL;
    }

    char *data = (char *)serve_malloc((size_t)size + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(data, 1, (size_t)size, f);
    fclose(f);

    data[read] = '\0';
    if (out_size) *out_size = read;
    return data;
}
