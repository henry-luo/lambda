/* Comprehensive zlog stub header for cross-compilation testing */
#ifndef ZLOG_H
#define ZLOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* zlog version */
#define ZLOG_VERSION_MAJOR 1
#define ZLOG_VERSION_MINOR 2
#define ZLOG_VERSION_MICRO 17

/* Return codes */
#define ZLOG_OK         0
#define ZLOG_LEVEL_TOO_HIGH -1
#define ZLOG_LEVEL_TOO_LOW  -2
#define ZLOG_WRONG_FORMAT   -3
#define ZLOG_WRITE_FAIL     -4
#define ZLOG_INIT_FAIL      -5
#define ZLOG_CATEGORY_NOT_FOUND -6

/* Log levels */
typedef enum {
    ZLOG_LEVEL_DEBUG = 20,
    ZLOG_LEVEL_INFO = 40,
    ZLOG_LEVEL_NOTICE = 60,
    ZLOG_LEVEL_WARN = 80,
    ZLOG_LEVEL_ERROR = 100,
    ZLOG_LEVEL_FATAL = 120
} zlog_level;

/* zlog category structure */
typedef struct zlog_category_s {
    char name[64];      /* category name */
    int level;          /* current log level */
    void *private_data; /* stub private data */
} zlog_category_t;

/* Basic zlog function declarations */
int zlog_init(const char *config);
void zlog_fini(void);
int zlog_reload(const char *config);
zlog_category_t* zlog_get_category(const char *cname);

/* Logging functions with level */
int zlog_put_mdc(const char *key, const char *value);
char* zlog_get_mdc(const char *key);
void zlog_remove_mdc(const char *key);
void zlog_clean_mdc(void);

/* Core logging functions */
int zlog_fatal(zlog_category_t *category, const char *format, ...);
int zlog_error(zlog_category_t *category, const char *format, ...);
int zlog_warn(zlog_category_t *category, const char *format, ...);
int zlog_notice(zlog_category_t *category, const char *format, ...);
int zlog_info(zlog_category_t *category, const char *format, ...);
int zlog_debug(zlog_category_t *category, const char *format, ...);

/* Variadic versions */
int zlog_vfatal(zlog_category_t *category, const char *format, va_list args);
int zlog_verror(zlog_category_t *category, const char *format, va_list args);
int zlog_vwarn(zlog_category_t *category, const char *format, va_list args);
int zlog_vnotice(zlog_category_t *category, const char *format, va_list args);
int zlog_vinfo(zlog_category_t *category, const char *format, va_list args);
int zlog_vdebug(zlog_category_t *category, const char *format, va_list args);

/* Level check functions */
int zlog_level_enabled(zlog_category_t *category, const int level);

/* Convenience macros for default category */
extern zlog_category_t *dzlog_default_category;

#define dzlog_fatal(format, args...) \
    zlog_fatal(dzlog_default_category, format, ##args)
#define dzlog_error(format, args...) \
    zlog_error(dzlog_default_category, format, ##args)
#define dzlog_warn(format, args...) \
    zlog_warn(dzlog_default_category, format, ##args)
#define dzlog_notice(format, args...) \
    zlog_notice(dzlog_default_category, format, ##args)
#define dzlog_info(format, args...) \
    zlog_info(dzlog_default_category, format, ##args)
#define dzlog_debug(format, args...) \
    zlog_debug(dzlog_default_category, format, ##args)

/* Default category initialization/finalization */
int dzlog_init(const char *config, const char *default_category);
void dzlog_fini(void);

/* Simple macros that use printf for stub implementation */
#ifndef ZLOG_REMOVE_DEBUG
#define dzlog_fatal_simple(format, args...) \
    printf("[FATAL] " format "\n", ##args)
#define dzlog_error_simple(format, args...) \
    printf("[ERROR] " format "\n", ##args)
#define dzlog_warn_simple(format, args...) \
    printf("[WARN] " format "\n", ##args)
#define dzlog_notice_simple(format, args...) \
    printf("[NOTICE] " format "\n", ##args)
#define dzlog_info_simple(format, args...) \
    printf("[INFO] " format "\n", ##args)
#define dzlog_debug_simple(format, args...) \
    printf("[DEBUG] " format "\n", ##args)
#else
#define dzlog_fatal_simple(format, args...)
#define dzlog_error_simple(format, args...)
#define dzlog_warn_simple(format, args...)
#define dzlog_notice_simple(format, args...)
#define dzlog_info_simple(format, args...)
#define dzlog_debug_simple(format, args...)
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZLOG_H */
