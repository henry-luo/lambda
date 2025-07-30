/**
 * @file utils.h
 * @brief utility functions for the HTTP/HTTPS server
 *
 * this file contains common helper functions, error handling,
 * and logging utilities used throughout the server implementation.
 */

#ifndef SERVE_UTILS_H
#define SERVE_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * log levels for server logging
 */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_level_t;

/**
 * error handling
 */

/**
 * set the last error message
 * @param format printf-style format string
 * @param ... format arguments
 */
void serve_set_error(const char *format, ...);

/**
 * get the last error message
 * @return error message string (thread-local)
 */
const char* serve_get_error(void);

/**
 * clear the last error message
 */
void serve_clear_error(void);

/**
 * logging functions
 */

/**
 * set the minimum log level
 * @param level minimum level to log
 */
void serve_set_log_level(log_level_t level);

/**
 * log a message at the specified level
 * @param level log level
 * @param format printf-style format string
 * @param ... format arguments
 */
void serve_log(log_level_t level, const char *format, ...);

/**
 * convenience macros for logging
 */
#define SERVE_LOG_DEBUG(...) serve_log(LOG_DEBUG, __VA_ARGS__)
#define SERVE_LOG_INFO(...)  serve_log(LOG_INFO, __VA_ARGS__)
#define SERVE_LOG_WARN(...)  serve_log(LOG_WARN, __VA_ARGS__)
#define SERVE_LOG_ERROR(...) serve_log(LOG_ERROR, __VA_ARGS__)
#define SERVE_LOG_FATAL(...) serve_log(LOG_FATAL, __VA_ARGS__)

/**
 * string utilities
 */

/**
 * safe string duplication with null check
 * @param str string to duplicate (can be NULL)
 * @return duplicated string or NULL
 */
char* serve_strdup(const char *str);

/**
 * case-insensitive string comparison
 * @param s1 first string
 * @param s2 second string
 * @return 0 if equal, non-zero otherwise
 */
int serve_strcasecmp(const char *s1, const char *s2);

/**
 * trim whitespace from string (modifies in place)
 * @param str string to trim
 * @return pointer to trimmed string
 */
char* serve_strtrim(char *str);

/**
 * url decode a string in place
 * @param str string to decode
 * @return length of decoded string
 */
size_t serve_url_decode(char *str);

/**
 * get file extension from path
 * @param path file path
 * @return pointer to extension (including dot) or NULL
 */
const char* serve_get_file_extension(const char *path);

/**
 * time utilities
 */

/**
 * get current timestamp as string (rfc 2822 format)
 * @param buffer buffer to write to (must be at least 32 bytes)
 * @return pointer to buffer
 */
char* serve_get_timestamp(char *buffer);

/**
 * get file modification time as http date string
 * @param filepath path to file
 * @param buffer buffer to write to (must be at least 32 bytes)
 * @return pointer to buffer or NULL on error
 */
char* serve_get_file_time(const char *filepath, char *buffer);

/**
 * memory utilities
 */

/**
 * safe memory allocation with zero initialization
 * @param size number of bytes to allocate
 * @return pointer to allocated memory or NULL
 */
void* serve_malloc(size_t size);

/**
 * safe memory reallocation
 * @param ptr existing pointer (can be NULL)
 * @param size new size
 * @return pointer to reallocated memory or NULL
 */
void* serve_realloc(void *ptr, size_t size);

/**
 * safe memory deallocation
 * @param ptr pointer to free (can be NULL)
 */
void serve_free(void *ptr);

/**
 * file utilities
 */

/**
 * check if file exists and is readable
 * @param filepath path to check
 * @return 1 if exists and readable, 0 otherwise
 */
int serve_file_exists(const char *filepath);

/**
 * get file size
 * @param filepath path to file
 * @return file size in bytes, -1 on error
 */
long serve_file_size(const char *filepath);

/**
 * read entire file into memory
 * @param filepath path to file
 * @param size pointer to store file size (can be NULL)
 * @return file contents or NULL on error (must be freed)
 */
char* serve_read_file(const char *filepath, size_t *size);

/**
 * mime type utilities
 */

/**
 * get mime type for file extension
 * @param extension file extension (including dot)
 * @return mime type string
 */
const char* serve_get_mime_type(const char *extension);

#ifdef __cplusplus
}
#endif

#endif /* SERVE_UTILS_H */
