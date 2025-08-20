/* Lambda Script Log Library - zlog-compatible implementation with log_ prefix */
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Configuration constants */
#define MAX_CATEGORIES 32
#define MAX_CONFIG_LINE 512
#define MAX_TIMESTAMP_LEN 32

/* Color codes for terminal output */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"
#define COLOR_BOLD    "\033[1m"

/* Global state */
static log_category_t categories[MAX_CATEGORIES];
static int categories_count = 0;
static int log_initialized = 0;
static int timestamps_enabled = 1;
static int colors_enabled = 0;

/* Default category */
static log_category_t default_category = {"default", LOG_LEVEL_DEBUG, NULL, 1};
log_category_t *log_default_category = &default_category;

/* Helper function to get current timestamp */
static void get_timestamp(char *buffer, size_t size) {
    if (!timestamps_enabled) {
        buffer[0] = '\0';
        return;
    }
    
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", timeinfo);
}

/* Helper function to get color for log level */
static const char* get_level_color(int level) {
    if (!colors_enabled) return "";
    
    switch (level) {
        case LOG_LEVEL_FATAL: return COLOR_BOLD COLOR_RED;
        case LOG_LEVEL_ERROR: return COLOR_RED;
        case LOG_LEVEL_WARN:  return COLOR_YELLOW;
        case LOG_LEVEL_NOTICE: return COLOR_CYAN;
        case LOG_LEVEL_INFO:  return COLOR_BLUE;
        case LOG_LEVEL_DEBUG: return COLOR_MAGENTA;
        default: return "";
    }
}

/* Convert log level to string */
const char* log_level_to_string(int level) {
    switch (level) {
        case LOG_LEVEL_FATAL: return "FATAL";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_NOTICE: return "NOTICE";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        default: return "UNKNOWN";
    }
}

/* Core logging implementation */
static int log_output(log_category_t *category, int level, const char *format, va_list args) {
    if (!category || !category->enabled) {
        return LOG_OK;
    }
    
    if (level < category->level) {
        return LOG_OK;
    }
    
    FILE *output = category->output ? category->output : stdout;
    char timestamp[MAX_TIMESTAMP_LEN];
    get_timestamp(timestamp, sizeof(timestamp));
    
    const char *level_str = log_level_to_string(level);
    const char *color = get_level_color(level);
    const char *reset_color = colors_enabled ? COLOR_RESET : "";
    
    // Print log prefix
    if (timestamps_enabled && timestamp[0]) {
        fprintf(output, "%s[%s] %s[%s]%s ", timestamp, level_str, color, category->name, reset_color);
    } else {
        fprintf(output, "%s[%s] [%s]%s ", color, level_str, category->name, reset_color);
    }
    
    // Print the actual message
    vfprintf(output, format, args);
    fprintf(output, "\n");
    fflush(output);
    
    return LOG_OK;
}

/* Initialize logging system */
int log_init(const char *config) {
    if (log_initialized) {
        return LOG_OK;
    }
    
    // Auto-detect if we should enable colors (if output is a TTY)
    colors_enabled = isatty(fileno(stdout));
    
    // Set default category output
    default_category.output = stdout;
    
    // Parse configuration if provided
    if (config && strlen(config) > 0) {
        if (log_parse_config_string(config) != LOG_OK) {
            fprintf(stderr, "[LOG] Warning: Failed to parse config, using defaults\n");
        }
    }
    
    log_initialized = 1;
    return LOG_OK;
}

/* Cleanup logging system */
void log_fini(void) {
    if (!log_initialized) {
        return;
    }
    
    // Close any opened files (except stdout/stderr)
    for (int i = 0; i < categories_count; i++) {
        if (categories[i].output && 
            categories[i].output != stdout && 
            categories[i].output != stderr) {
            fclose(categories[i].output);
        }
    }
    
    categories_count = 0;
    log_initialized = 0;
}

/* Reload configuration */
int log_reload(const char *config) {
    log_fini();
    return log_init(config);
}

/* Get or create a category */
log_category_t* log_get_category(const char *cname) {
    if (!cname) {
        return &default_category;
    }
    
    // Look for existing category
    for (int i = 0; i < categories_count; i++) {
        if (strcmp(categories[i].name, cname) == 0) {
            return &categories[i];
        }
    }
    
    // Create new category if space available
    if (categories_count < MAX_CATEGORIES) {
        log_category_t *cat = &categories[categories_count++];
        strncpy(cat->name, cname, sizeof(cat->name) - 1);
        cat->name[sizeof(cat->name) - 1] = '\0';
        cat->level = LOG_LEVEL_DEBUG;
        cat->output = stdout;
        cat->enabled = 1;
        return cat;
    }
    
    // Return default if no space
    return &default_category;
}

/* Level management */
int log_level_enabled(log_category_t *category, const int level) {
    if (!category || !category->enabled) return 0;
    return (level >= category->level);
}

void log_set_level(log_category_t *category, int level) {
    if (category) {
        category->level = level;
    }
}

void log_set_output(log_category_t *category, FILE *output) {
    if (category) {
        category->output = output;
    }
}

/* Utility functions */
void log_enable_timestamps(int enable) {
    timestamps_enabled = enable;
}

void log_enable_colors(int enable) {
    colors_enabled = enable;
}

/* Core logging functions with category parameter */
int clog_fatal(log_category_t *category, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = clog_vfatal(category, format, args);
    va_end(args);
    return ret;
}

int clog_error(log_category_t *category, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = clog_verror(category, format, args);
    va_end(args);
    return ret;
}

int clog_warn(log_category_t *category, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = clog_vwarn(category, format, args);
    va_end(args);
    return ret;
}

int clog_notice(log_category_t *category, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = clog_vnotice(category, format, args);
    va_end(args);
    return ret;
}

int clog_info(log_category_t *category, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = clog_vinfo(category, format, args);
    va_end(args);
    return ret;
}

int clog_debug(log_category_t *category, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = clog_vdebug(category, format, args);
    va_end(args);
    return ret;
}

/* Default category logging functions (convenient API) */
int log_fatal(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = log_vfatal(format, args);
    va_end(args);
    return ret;
}

int log_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = log_verror(format, args);
    va_end(args);
    return ret;
}

int log_warn(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = log_vwarn(format, args);
    va_end(args);
    return ret;
}

int log_notice(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = log_vnotice(format, args);
    va_end(args);
    return ret;
}

int log_info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = log_vinfo(format, args);
    va_end(args);
    return ret;
}

int log_debug(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = log_vdebug(format, args);
    va_end(args);
    return ret;
}

/* Variadic versions with category parameter */
int clog_vfatal(log_category_t *category, const char *format, va_list args) {
    return log_output(category, LOG_LEVEL_FATAL, format, args);
}

int clog_verror(log_category_t *category, const char *format, va_list args) {
    return log_output(category, LOG_LEVEL_ERROR, format, args);
}

int clog_vwarn(log_category_t *category, const char *format, va_list args) {
    return log_output(category, LOG_LEVEL_WARN, format, args);
}

int clog_vnotice(log_category_t *category, const char *format, va_list args) {
    return log_output(category, LOG_LEVEL_NOTICE, format, args);
}

int clog_vinfo(log_category_t *category, const char *format, va_list args) {
    return log_output(category, LOG_LEVEL_INFO, format, args);
}

int clog_vdebug(log_category_t *category, const char *format, va_list args) {
    return log_output(category, LOG_LEVEL_DEBUG, format, args);
}

/* Default category variadic versions */
int log_vfatal(const char *format, va_list args) {
    return log_output(log_default_category, LOG_LEVEL_FATAL, format, args);
}

int log_verror(const char *format, va_list args) {
    return log_output(log_default_category, LOG_LEVEL_ERROR, format, args);
}

int log_vwarn(const char *format, va_list args) {
    return log_output(log_default_category, LOG_LEVEL_WARN, format, args);
}

int log_vnotice(const char *format, va_list args) {
    return log_output(log_default_category, LOG_LEVEL_NOTICE, format, args);
}

int log_vinfo(const char *format, va_list args) {
    return log_output(log_default_category, LOG_LEVEL_INFO, format, args);
}

int log_vdebug(const char *format, va_list args) {
    return log_output(log_default_category, LOG_LEVEL_DEBUG, format, args);
}

/* Default category functions */
int log_default_init(const char *config, const char *default_category_name) {
    int ret = log_init(config);
    if (ret == LOG_OK && default_category_name) {
        log_default_category = log_get_category(default_category_name);
    }
    return ret;
}

void log_default_fini(void) {
    log_fini();
    log_default_category = &default_category;
}

/* Simple configuration parsing */
int log_parse_config_string(const char *config) {
    if (!config) return LOG_OK;
    
    // Simple key=value parsing
    char *config_copy = strdup(config);
    if (!config_copy) return LOG_INIT_FAIL;
    
    char *line = strtok(config_copy, "\n;");
    while (line) {
        // Skip whitespace and comments
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '#' || *line == '\0') {
            line = strtok(NULL, "\n;");
            continue;
        }
        
        // Parse key=value
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *key = line;
            char *value = eq + 1;
            
            // Remove trailing whitespace from key
            char *end = key + strlen(key) - 1;
            while (end > key && (*end == ' ' || *end == '\t')) {
                *end = '\0';
                end--;
            }
            
            // Remove leading whitespace from value
            while (*value == ' ' || *value == '\t') value++;
            
            // Process configuration options
            if (strcmp(key, "timestamps") == 0) {
                timestamps_enabled = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
            } else if (strcmp(key, "colors") == 0) {
                colors_enabled = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
            } else if (strcmp(key, "level") == 0) {
                int level = LOG_LEVEL_DEBUG;
                if (strcmp(value, "fatal") == 0) level = LOG_LEVEL_FATAL;
                else if (strcmp(value, "error") == 0) level = LOG_LEVEL_ERROR;
                else if (strcmp(value, "warn") == 0) level = LOG_LEVEL_WARN;
                else if (strcmp(value, "notice") == 0) level = LOG_LEVEL_NOTICE;
                else if (strcmp(value, "info") == 0) level = LOG_LEVEL_INFO;
                else if (strcmp(value, "debug") == 0) level = LOG_LEVEL_DEBUG;
                log_set_level(&default_category, level);
            }
        }
        
        line = strtok(NULL, "\n;");
    }
    
    free(config_copy);
    return LOG_OK;
}

int log_parse_config_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        return LOG_INIT_FAIL;
    }
    
    char buffer[MAX_CONFIG_LINE];
    char *config_string = malloc(1);
    size_t config_len = 0;
    
    if (!config_string) {
        fclose(file);
        return LOG_INIT_FAIL;
    }
    config_string[0] = '\0';
    
    while (fgets(buffer, sizeof(buffer), file)) {
        size_t line_len = strlen(buffer);
        config_string = realloc(config_string, config_len + line_len + 1);
        if (!config_string) {
            fclose(file);
            return LOG_INIT_FAIL;
        }
        strcpy(config_string + config_len, buffer);
        config_len += line_len;
    }
    
    fclose(file);
    
    int ret = log_parse_config_string(config_string);
    free(config_string);
    return ret;
}
