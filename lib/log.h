/* Lambda Script Log Library - zlog-compatible API with log_ prefix */
#ifndef LOG_H
#define LOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* log version */
#define LOG_VERSION_MAJOR 1
#define LOG_VERSION_MINOR 0
#define LOG_VERSION_MICRO 0

/* Return codes */
#define LOG_OK              0
#define LOG_LEVEL_TOO_HIGH -1
#define LOG_LEVEL_TOO_LOW  -2
#define LOG_WRONG_FORMAT   -3
#define LOG_WRITE_FAIL     -4
#define LOG_INIT_FAIL      -5
#define LOG_CATEGORY_NOT_FOUND -6

/* Log levels */
typedef enum {
    LOG_LEVEL_DEBUG = 20,
    LOG_LEVEL_INFO = 40,
    LOG_LEVEL_NOTICE = 60,
    LOG_LEVEL_WARN = 80,
    LOG_LEVEL_ERROR = 100,
    LOG_LEVEL_FATAL = 120
} log_level;

/* log category structure */
typedef struct log_category_s {
    char name[64];      /* category name */
    int level;          /* current log level */
    FILE *output;       /* output stream (stdout, stderr, or file) */
    int enabled;        /* whether this category is enabled */
} log_category_t;

/* Basic log function declarations */
int log_init(const char *config);
void log_fini(void);
int log_reload(const char *config);
log_category_t* log_get_category(const char *cname);

/* Core logging functions with category parameter */
int clog_fatal(log_category_t *category, const char *format, ...);
int clog_error(log_category_t *category, const char *format, ...);
int clog_warn(log_category_t *category, const char *format, ...);
int clog_notice(log_category_t *category, const char *format, ...);
int clog_info(log_category_t *category, const char *format, ...);
int clog_debug(log_category_t *category, const char *format, ...);

/* Variadic versions with category parameter */
int clog_vfatal(log_category_t *category, const char *format, va_list args);
int clog_verror(log_category_t *category, const char *format, va_list args);
int clog_vwarn(log_category_t *category, const char *format, va_list args);
int clog_vnotice(log_category_t *category, const char *format, va_list args);
int clog_vinfo(log_category_t *category, const char *format, va_list args);
int clog_vdebug(log_category_t *category, const char *format, va_list args);

/* Default category logging functions (convenient API) */
int log_fatal(const char *format, ...);
int log_error(const char *format, ...);
int log_warn(const char *format, ...);
int log_notice(const char *format, ...);
int log_info(const char *format, ...);
int log_debug(const char *format, ...);

/* Default category variadic versions */
int log_vfatal(const char *format, va_list args);
int log_verror(const char *format, va_list args);
int log_vwarn(const char *format, va_list args);
int log_vnotice(const char *format, va_list args);
int log_vinfo(const char *format, va_list args);
int log_vdebug(const char *format, va_list args);

/* Level check functions */
int log_level_enabled(log_category_t *category, const int level);
void log_set_level(log_category_t *category, int level);
void log_set_output(log_category_t *category, FILE *output);

/* Default category support */
extern log_category_t *log_default_category;

/* Default category initialization/finalization with dzlog-compatible names */
int log_default_init(const char *config, const char *default_category);
void log_default_fini(void);

/* Utility functions */
void log_enable_timestamps(int enable);
void log_enable_colors(int enable);
const char* log_level_to_string(int level);

/* Configuration parsing */
int log_parse_config_file(const char *filename);
int log_parse_config_string(const char *config);

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
