/**
 * @file utils.c
 * @brief utility function implementations for the HTTP/HTTPS server
 */

#include "utils.h"
#include "../str.h"
#include <stdarg.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>

// thread-local error storage (simplified for single-threaded use)
static char error_buffer[512] = {0};

// current log level
static log_level_t current_log_level = LOG_INFO;

// log level names
static const char* log_level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

/**
 * error handling implementation
 */

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

/**
 * logging implementation
 */

void serve_set_log_level(log_level_t level) {
    current_log_level = level;
}

void serve_log(log_level_t level, const char *format, ...) {
    if (level < current_log_level) {
        return;
    }

    // get current time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    // print timestamp and level
    printf("[%04d-%02d-%02d %02d:%02d:%02d] [%s] ",
           tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
           tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
           log_level_names[level]);

    // print message
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n");
    fflush(stdout);
}

/**
 * string utilities implementation
 */

char* serve_strdup(const char *str) {
    if (!str) {
        return NULL;
    }

    size_t len = strlen(str);
    char *dup = serve_malloc(len + 1);
    if (dup) {
        memcpy(dup, str, len + 1);
    }
    return dup;
}

int serve_strcasecmp(const char *s1, const char *s2) {
    if (!s1 || !s2) {
        return s1 - s2;
    }
    return str_icmp(s1, strlen(s1), s2, strlen(s2));
}

char* serve_strtrim(char *str) {
    if (!str) {
        return NULL;
    }

    // trim leading whitespace
    while (isspace(*str)) {
        str++;
    }

    if (*str == '\0') {
        return str;
    }

    // trim trailing whitespace
    char *end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) {
        end--;
    }
    *(end + 1) = '\0';

    return str;
}

size_t serve_url_decode(char *str) {
    if (!str) {
        return 0;
    }

    char *src = str;
    char *dst = str;

    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            // convert hex digits to character
            int high = (src[1] >= '0' && src[1] <= '9') ? src[1] - '0' :
                      (src[1] >= 'A' && src[1] <= 'F') ? src[1] - 'A' + 10 :
                      (src[1] >= 'a' && src[1] <= 'f') ? src[1] - 'a' + 10 : -1;
            int low = (src[2] >= '0' && src[2] <= '9') ? src[2] - '0' :
                     (src[2] >= 'A' && src[2] <= 'F') ? src[2] - 'A' + 10 :
                     (src[2] >= 'a' && src[2] <= 'f') ? src[2] - 'a' + 10 : -1;

            if (high >= 0 && low >= 0) {
                *dst++ = (char)(high * 16 + low);
                src += 3;
            } else {
                *dst++ = *src++;
            }
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    return dst - str;
}

const char* serve_get_file_extension(const char *path) {
    if (!path) {
        return NULL;
    }

    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');

    // make sure the dot is after the last slash (not in directory name)
    if (dot && (!slash || dot > slash)) {
        return dot;
    }

    return NULL;
}

/**
 * time utilities implementation
 */

char* serve_get_timestamp(char *buffer) {
    if (!buffer) {
        return NULL;
    }

    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);

    strftime(buffer, 32, "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    return buffer;
}

char* serve_get_file_time(const char *filepath, char *buffer) {
    if (!filepath || !buffer) {
        return NULL;
    }

    struct stat st;
    if (stat(filepath, &st) != 0) {
        return NULL;
    }

    struct tm *tm_info = gmtime(&st.st_mtime);
    strftime(buffer, 32, "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    return buffer;
}

/**
 * memory utilities implementation
 */

void* serve_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    void *ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void* serve_realloc(void *ptr, size_t size) {
    if (size == 0) {
        serve_free(ptr);
        return NULL;
    }

    return realloc(ptr, size);
}

void serve_free(void *ptr) {
    if (ptr) {
        free(ptr);
    }
}

/**
 * file utilities implementation
 */

int serve_file_exists(const char *filepath) {
    if (!filepath) {
        return 0;
    }

    return access(filepath, R_OK) == 0;
}

long serve_file_size(const char *filepath) {
    if (!filepath) {
        return -1;
    }

    struct stat st;
    if (stat(filepath, &st) != 0) {
        return -1;
    }

    return st.st_size;
}

char* serve_read_file(const char *filepath, size_t *size) {
    if (!filepath) {
        return NULL;
    }

    FILE *file = fopen(filepath, "rb");
    if (!file) {
        serve_set_error("failed to open file: %s", filepath);
        return NULL;
    }

    // get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size < 0) {
        fclose(file);
        serve_set_error("failed to get file size: %s", filepath);
        return NULL;
    }

    // allocate buffer
    char *buffer = serve_malloc(file_size + 1);
    if (!buffer) {
        fclose(file);
        serve_set_error("failed to allocate memory for file: %s", filepath);
        return NULL;
    }

    // read file
    size_t bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        serve_free(buffer);
        serve_set_error("failed to read file: %s", filepath);
        return NULL;
    }

    buffer[file_size] = '\0';

    if (size) {
        *size = file_size;
    }

    return buffer;
}

/**
 * mime type mapping table
 */
static const struct {
    const char *extension;
    const char *mime_type;
} mime_types[] = {
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".xml", "application/xml"},
    {".txt", "text/plain"},
    {".md", "text/markdown"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".pdf", "application/pdf"},
    {".zip", "application/zip"},
    {".tar", "application/x-tar"},
    {".gz", "application/gzip"},
    {NULL, NULL}
};

const char* serve_get_mime_type(const char *extension) {
    if (!extension) {
        return "application/octet-stream";
    }

    for (int i = 0; mime_types[i].extension; i++) {
        if (serve_strcasecmp(extension, mime_types[i].extension) == 0) {
            return mime_types[i].mime_type;
        }
    }

    return "application/octet-stream";
}
