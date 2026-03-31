/**
 * @file serve_utils.hpp
 * @brief Utility functions for the Lambda web server
 *
 * Memory management wrappers, string utilities, error handling,
 * and time formatting helpers. Migrated from lib/serve/utils.h.
 */

#pragma once

#include <stddef.h>
#include <stdarg.h>

// ============================================================================
// Memory management (thin wrappers for tracking)
// ============================================================================

void* serve_malloc(size_t size);
void* serve_calloc(size_t count, size_t size);
void* serve_realloc(void *ptr, size_t size);
void  serve_free(void *ptr);
char* serve_strdup(const char *str);

// ============================================================================
// Error handling (thread-local error buffer)
// ============================================================================

void        serve_set_error(const char *format, ...);
const char* serve_get_error(void);
void        serve_clear_error(void);

// ============================================================================
// String utilities
// ============================================================================

// case-insensitive string comparison (returns 0 if equal)
int serve_strcasecmp(const char *s1, const char *s2);

// in-place trim leading and trailing whitespace. returns str.
char* serve_strtrim(char *str);

// URL-decode in-place (%XX → byte, + → space). returns new length.
size_t serve_url_decode(char *str);

// get file extension from path (returns pointer into path, or "")
const char* serve_get_file_extension(const char *path);

// ============================================================================
// Time utilities
// ============================================================================

// write current timestamp in HTTP date format to buffer. returns buffer.
// buffer must be at least 64 bytes.
char* serve_http_date(char *buffer, size_t bufsize);

// format file modification time. returns buffer.
char* serve_file_mtime_str(const char *filepath, char *buffer, size_t bufsize);

// ============================================================================
// File utilities
// ============================================================================

int   serve_file_exists(const char *filepath);
long  serve_file_size(const char *filepath);
char* serve_read_file(const char *filepath, size_t *out_size);
