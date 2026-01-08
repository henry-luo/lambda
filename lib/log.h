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

/* Thread-local indentation support */
#ifdef _WIN32
    #define THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
    #define THREAD_LOCAL __thread
#else
    #define THREAD_LOCAL _Thread_local
#endif

/* Maximum indentation level (default 20 levels = 40 spaces) */
#ifndef LOG_MAX_INDENT_LEVEL
#define LOG_MAX_INDENT_LEVEL 20
#endif

/* Thread-local indentation variables */
extern THREAD_LOCAL int log_indent;

/* Indentation control macros */
#define log_enter() do { \
    if (log_indent < LOG_MAX_INDENT_LEVEL * 2) log_indent += 2; \
} while(0)

#define log_leave() do { \
    if (log_indent >= 2) log_indent -= 2; \
} while(0)

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

/* Format configuration structure */
typedef struct log_format_s {
    char name[64];           /* format name */
    char pattern[256];       /* format pattern */
    int show_timestamp;      /* show timestamp */
    int show_date;          /* show date part of timestamp */
    int show_category;      /* show category name */
    int hide_default_category; /* hide category if it's "default" */
} log_format_t;

/* Rule configuration structure */
typedef struct log_rule_s {
    char category[64];      /* category name */
    int level;              /* minimum log level */
    char output_file[256];  /* output file path, empty for stdout/stderr */
    char format_name[64];   /* format to use */
} log_rule_t;

/* log category structure */
typedef struct log_category_s {
    char name[64];          /* category name */
    int level;              /* current log level */
    FILE *output;           /* output stream (stdout, stderr, or file) */
    int enabled;            /* whether this category is enabled */
    log_format_t *format;   /* format configuration */
    char output_filename[256]; /* output file name (for color decision) */
} log_category_t;

/* Basic log function declarations */
int log_init(const char *config);
void log_finish(void);
int log_reload(const char *config);
log_category_t* log_get_category(const char *cname);

/* Core logging functions with category parameter */
int clog_fatal(log_category_t *category, const char *format, ...);
int clog_error(log_category_t *category, const char *format, ...);
int clog_warn(log_category_t *category, const char *format, ...);
int clog_notice(log_category_t *category, const char *format, ...);

/*
 * Release build optimization: clog_debug() and clog_info() are stripped
 * When NDEBUG is defined (release builds), these become no-ops.
 * LOG_IMPL is defined by log.c to prevent macro replacement of function definitions.
 */
#if defined(NDEBUG) && !defined(LOG_IMPL)
    #define clog_info(category, ...) ((void)0)
    #define clog_debug(category, ...) ((void)0)
#else
    int clog_info(log_category_t *category, const char *format, ...);
    int clog_debug(log_category_t *category, const char *format, ...);
#endif

/* Variadic versions with category parameter */
int clog_vfatal(log_category_t *category, const char *format, va_list args);
int clog_verror(log_category_t *category, const char *format, va_list args);
int clog_vwarn(log_category_t *category, const char *format, va_list args);
int clog_vnotice(log_category_t *category, const char *format, va_list args);

#if defined(NDEBUG) && !defined(LOG_IMPL)
    #define clog_vinfo(category, format, args) ((void)0)
    #define clog_vdebug(category, format, args) ((void)0)
#else
    int clog_vinfo(log_category_t *category, const char *format, va_list args);
    int clog_vdebug(log_category_t *category, const char *format, va_list args);
#endif

/* Default category logging functions (convenient API) */
int log_fatal(const char *format, ...);
int log_error(const char *format, ...);
int log_warn(const char *format, ...);
int log_notice(const char *format, ...);

/*
 * Release build optimization: log_debug() and log_info() are stripped
 * When NDEBUG is defined (release builds), these become no-ops that the
 * compiler will completely eliminate, reducing binary size and overhead.
 * LOG_IMPL is defined by log.c to prevent macro replacement of function definitions.
 */
#if defined(NDEBUG) && !defined(LOG_IMPL)
    /* Release build: strip debug and info logging completely */
    #define log_info(...) ((void)0)
    #define log_debug(...) ((void)0)
#else
    /* Debug build: use actual logging functions */
    int log_info(const char *format, ...);
    int log_debug(const char *format, ...);
#endif

/* Default category variadic versions */
int log_vfatal(const char *format, va_list args);
int log_verror(const char *format, va_list args);
int log_vwarn(const char *format, va_list args);
int log_vnotice(const char *format, va_list args);

#if defined(NDEBUG) && !defined(LOG_IMPL)
    #define log_vinfo(format, args) ((void)0)
    #define log_vdebug(format, args) ((void)0)
#else
    int log_vinfo(const char *format, va_list args);
    int log_vdebug(const char *format, va_list args);
#endif

/* Level check functions */
int log_level_enabled(log_category_t *category, const int level);
void log_set_level(log_category_t *category, int level);
void log_set_output(log_category_t *category, FILE *output);

/* Default category support */
extern log_category_t *log_default_category;

/* Default category initialization/finalization with dzlog-compatible names */
int log_default_init(const char *config, const char *default_category);
void log_default_finish(void);

/* Utility functions */
void log_enable_timestamps(int enable);
void log_enable_colors(int enable);
void log_disable_all(void);  // Disable all logging categories
const char* log_level_to_string(int level);

/* Format management */
log_format_t* log_get_format(const char *name);
int log_add_format(const char *name, const char *pattern);
void log_set_default_format(const char *pattern);

/* Configuration parsing */
int log_parse_config_file(const char *filename);
int log_parse_config_string(const char *config);

/* Indentation management functions */
void log_set_indent(int indent);
int log_get_indent(void);
void log_reset_indent(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
