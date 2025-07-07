/* Comprehensive zlog stub implementation */
#include "../include/zlog.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Global variables for stub */
static zlog_category_t stub_category = {"stub", ZLOG_LEVEL_DEBUG, NULL};
zlog_category_t *dzlog_default_category = &stub_category;
static int zlog_initialized = 0;

/* Simple timestamp function for stub */
static void stub_timestamp(char *buffer, size_t size) {
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", timeinfo);
}

/* Core zlog functions */
int zlog_init(const char *config) {
    (void)config; /* Unused in stub */
    if (!zlog_initialized) {
        printf("[ZLOG] Stub initialization with config: %s\n", 
               config ? config : "(null)");
        zlog_initialized = 1;
    }
    return ZLOG_OK;
}

void zlog_fini(void) {
    if (zlog_initialized) {
        printf("[ZLOG] Stub finalization\n");
        zlog_initialized = 0;
    }
}

int zlog_reload(const char *config) {
    (void)config; /* Unused in stub */
    printf("[ZLOG] Stub reload (no-op)\n");
    return ZLOG_OK;
}

zlog_category_t* zlog_get_category(const char *cname) {
    if (cname) {
        strncpy(stub_category.name, cname, sizeof(stub_category.name) - 1);
        stub_category.name[sizeof(stub_category.name) - 1] = '\0';
    }
    return &stub_category;
}

/* MDC functions (no-op in stub) */
int zlog_put_mdc(const char *key, const char *value) {
    (void)key; (void)value;
    return ZLOG_OK;
}

char* zlog_get_mdc(const char *key) {
    (void)key;
    return NULL;
}

void zlog_remove_mdc(const char *key) {
    (void)key;
}

void zlog_clean_mdc(void) {
    /* No-op */
}

/* Level check function */
int zlog_level_enabled(zlog_category_t *category, const int level) {
    if (!category) return 0;
    return (level >= category->level);
}

/* Core logging functions with formatted output */
static int stub_log(const char *level_str, zlog_category_t *category, 
                   const char *format, va_list args) {
    char timestamp[32];
    stub_timestamp(timestamp, sizeof(timestamp));
    
    printf("[%s] %s [%s] ", timestamp, level_str, 
           category ? category->name : "default");
    vprintf(format, args);
    printf("\n");
    fflush(stdout);
    return ZLOG_OK;
}

int zlog_fatal(zlog_category_t *category, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = zlog_vfatal(category, format, args);
    va_end(args);
    return ret;
}

int zlog_error(zlog_category_t *category, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = zlog_verror(category, format, args);
    va_end(args);
    return ret;
}

int zlog_warn(zlog_category_t *category, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = zlog_vwarn(category, format, args);
    va_end(args);
    return ret;
}

int zlog_notice(zlog_category_t *category, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = zlog_vnotice(category, format, args);
    va_end(args);
    return ret;
}

int zlog_info(zlog_category_t *category, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = zlog_vinfo(category, format, args);
    va_end(args);
    return ret;
}

int zlog_debug(zlog_category_t *category, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = zlog_vdebug(category, format, args);
    va_end(args);
    return ret;
}

/* Variadic versions */
int zlog_vfatal(zlog_category_t *category, const char *format, va_list args) {
    return stub_log("FATAL", category, format, args);
}

int zlog_verror(zlog_category_t *category, const char *format, va_list args) {
    return stub_log("ERROR", category, format, args);
}

int zlog_vwarn(zlog_category_t *category, const char *format, va_list args) {
    return stub_log("WARN", category, format, args);
}

int zlog_vnotice(zlog_category_t *category, const char *format, va_list args) {
    return stub_log("NOTICE", category, format, args);
}

int zlog_vinfo(zlog_category_t *category, const char *format, va_list args) {
    return stub_log("INFO", category, format, args);
}

int zlog_vdebug(zlog_category_t *category, const char *format, va_list args) {
    return stub_log("DEBUG", category, format, args);
}

/* Default category functions */
int dzlog_init(const char *config, const char *default_category) {
    int ret = zlog_init(config);
    if (ret == ZLOG_OK && default_category) {
        dzlog_default_category = zlog_get_category(default_category);
    }
    return ret;
}

void dzlog_fini(void) {
    zlog_fini();
    dzlog_default_category = &stub_category;
}
