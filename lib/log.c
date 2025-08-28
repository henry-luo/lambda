/* Lambda Script Log Library - zlog-compatible implementation with log_ prefix */
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>  // for bool type

/* Cross-platform strcasecmp */
#ifndef strcasecmp
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
#endif

/* Thread-local indentation variables */
THREAD_LOCAL int log_indent = 0;
THREAD_LOCAL char log_indent_buffer[LOG_MAX_INDENT_LEVEL * 2 + 1] = {0};

/* Forward declarations */
static int log_parse_zlog_config(const char *config);
static int log_parse_simple_config(const char *config);

/* Configuration constants */
#define MAX_CATEGORIES 32
#define MAX_FORMATS 16
#define MAX_RULES 64
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
static log_format_t formats[MAX_FORMATS];
static log_rule_t rules[MAX_RULES];
static int categories_count = 0;
static int formats_count = 0;
static int rules_count = 0;
static int log_initialized = 0;
static int timestamps_enabled = 1;
static int colors_enabled = 0;

/* Default format configuration */
static log_format_t default_format = {
    "default", 
    "%T %L %C %I%m%n",  /* time level category indentation message newline */
    1,  /* show_timestamp */
    0,  /* show_date */
    1,  /* show_category */
    1   /* hide_default_category */
};

/* Default category */
static log_category_t default_category = {"default", LOG_LEVEL_DEBUG, NULL, 1, &default_format, ""};
log_category_t *log_default_category = &default_category;

/* Helper function to get current timestamp */
static void get_timestamp(char *buffer, size_t size, int show_date) {
    if (!timestamps_enabled) {
        buffer[0] = '\0';
        return;
    }
    
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    
    if (show_date) {
        strftime(buffer, size, "%Y-%m-%d %H:%M:%S", timeinfo);
    } else {
        strftime(buffer, size, "%H:%M:%S", timeinfo);
    }
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

/* Format management functions */
log_format_t* log_get_format(const char *name) {
    if (!name) return &default_format;
    
    for (int i = 0; i < formats_count; i++) {
        if (strcmp(formats[i].name, name) == 0) {
            return &formats[i];
        }
    }
    return &default_format;
}

int log_add_format(const char *name, const char *pattern) {
    if (!name || !pattern || formats_count >= MAX_FORMATS) {
        return LOG_INIT_FAIL;
    }
    
    log_format_t *fmt = &formats[formats_count++];
    strncpy(fmt->name, name, sizeof(fmt->name) - 1);
    fmt->name[sizeof(fmt->name) - 1] = '\0';
    strncpy(fmt->pattern, pattern, sizeof(fmt->pattern) - 1);
    fmt->pattern[sizeof(fmt->pattern) - 1] = '\0';
    
    // Parse pattern to set flags
    fmt->show_timestamp = (strstr(pattern, "%H") || strstr(pattern, "%T") || strstr(pattern, "%F")) ? 1 : 0;
    fmt->show_date = strstr(pattern, "%F") ? 1 : 0;
    fmt->show_category = strstr(pattern, "%C") ? 1 : 0;
    fmt->hide_default_category = 1; // default behavior
    
    return LOG_OK;
}

void log_set_default_format(const char *pattern) {
    if (pattern) {
        strncpy(default_format.pattern, pattern, sizeof(default_format.pattern) - 1);
        default_format.pattern[sizeof(default_format.pattern) - 1] = '\0';
        
        // Update flags
        default_format.show_timestamp = (strstr(pattern, "%H") || strstr(pattern, "%T") || strstr(pattern, "%F")) ? 1 : 0;
        default_format.show_date = strstr(pattern, "%F") ? 1 : 0;
        default_format.show_category = strstr(pattern, "%C") ? 1 : 0;
    }
}

/* Helper function to determine if a file should use colors based on extension */
static bool should_use_colors_for_file(log_category_t *category) {
    if (!category || !category->output_filename[0]) {
        return false;  // No filename stored
    }
    
    // Check file extension
    const char *filename = category->output_filename;
    size_t len = strlen(filename);
    
    // Check for .log extension (use colors)
    if (len > 4 && strcasecmp(filename + len - 4, ".log") == 0) {
        return true;
    }
    
    // Check for .txt extension (no colors)
    if (len > 4 && strcasecmp(filename + len - 4, ".txt") == 0) {
        return false;
    }
    
    // Default: no colors for other file types
    return false;
}

/* Helper function to get indentation string (thread-local) */
static const char* get_indentation_string(void) {
    // Ensure indentation is within bounds
    int actual_indent = (log_indent > LOG_MAX_INDENT_LEVEL * 2) ? LOG_MAX_INDENT_LEVEL * 2 : log_indent;
    actual_indent = (actual_indent < 0) ? 0 : actual_indent;
    
    // Build indentation buffer if needed
    static THREAD_LOCAL int last_indent = -1;
    if (last_indent != actual_indent) {
        memset(log_indent_buffer, ' ', actual_indent);
        log_indent_buffer[actual_indent] = '\0';
        last_indent = actual_indent;
    }
    
    return log_indent_buffer;
}

/* Helper function to format log message according to pattern */
static void format_log_message(char *output, size_t output_size, log_format_t *format,
                              const char *timestamp, const char *level_str, 
                              const char *category_name, const char *color, 
                              const char *reset_color, const char *message) {
    if (!format || !format->pattern[0]) {
        // Fallback to simple format
        snprintf(output, output_size, "%s%s[%s] [%s]%s %s", 
                color, timestamp[0] ? timestamp : "", level_str, category_name, reset_color, message);
        return;
    }
    
    const char *p = format->pattern;
    char *out = output;
    size_t remaining = output_size - 1;
    
    while (*p && remaining > 0) {
        if (*p == '%' && *(p + 1)) {
            p++; // skip %
            switch (*p) {
                case 'H': // time (HH:MM:SS)
                case 'T': // time
                    if (format->show_timestamp && timestamp[0]) {
                        int len = snprintf(out, remaining, "%s", timestamp);
                        if (len > 0 && len < remaining) {
                            out += len;
                            remaining -= len;
                        }
                    }
                    break;
                case 'F': // full timestamp with date
                    if (format->show_timestamp && timestamp[0]) {
                        int len = snprintf(out, remaining, "%s", timestamp);
                        if (len > 0 && len < remaining) {
                            out += len;
                            remaining -= len;
                        }
                    }
                    break;
                case 'L': // log level
                    {
                        int len = snprintf(out, remaining, "%s[%s]%s", color, level_str, reset_color);
                        if (len > 0 && len < remaining) {
                            out += len;
                            remaining -= len;
                        }
                    }
                    break;
                case 'C': // category
                    if (format->show_category) {
                        // Hide default category if configured
                        if (!(format->hide_default_category && strcmp(category_name, "default") == 0)) {
                            int len = snprintf(out, remaining, "[%s]", category_name);
                            if (len > 0 && len < remaining) {
                                out += len;
                                remaining -= len;
                            }
                        }
                    }
                    break;
                case 'I': // indentation
                    {
                        const char *indent = get_indentation_string();
                        int len = snprintf(out, remaining, "%s", indent);
                        if (len > 0 && len < remaining) {
                            out += len;
                            remaining -= len;
                        }
                    }
                    break;
                case 'm': // message
                    {
                        int len = snprintf(out, remaining, "%s", message);
                        if (len > 0 && len < remaining) {
                            out += len;
                            remaining -= len;
                        }
                    }
                    break;
                case 'n': // newline
                    if (remaining > 0) {
                        *out++ = '\n';
                        remaining--;
                    }
                    break;
                case '%': // literal %
                    if (remaining > 0) {
                        *out++ = '%';
                        remaining--;
                    }
                    break;
                default:
                    // Unknown format specifier, copy literally
                    if (remaining > 1) {
                        *out++ = '%';
                        *out++ = *p;
                        remaining -= 2;
                    }
                    break;
            }
        } else {
            *out++ = *p;
            remaining--;
        }
        p++;
    }
    *out = '\0';
}

/* Helper function to write formatted log message to a stream */
static void write_log_message_to_stream(FILE *stream, log_category_t *category,
                                       const char *timestamp, const char *level_str, 
                                       const char *color, const char *reset_color, 
                                       const char *message, bool use_colors) {
    char formatted_message[1024];
    
    // Use colors if requested for this specific stream
    const char *actual_color = use_colors ? color : "";
    const char *actual_reset = use_colors ? reset_color : "";
    
    format_log_message(formatted_message, sizeof(formatted_message), 
                      category->format, timestamp, level_str, 
                      category->name, actual_color, actual_reset, message);
    
    // Remove trailing newline if it exists (we'll add our own)
    size_t len = strlen(formatted_message);
    if (len > 0 && formatted_message[len - 1] == '\n') {
        formatted_message[len - 1] = '\0';
    }
    
    fprintf(stream, "%s\n", formatted_message);
    fflush(stream);
}

/* Core logging implementation */
static int log_output(log_category_t *category, int level, const char *format, va_list args) {
    if (!category || !category->enabled) { return LOG_OK; }
    if (level < category->level) { return LOG_OK; }

    // Format the user message first
    char user_message[4096];
    int n = vsnprintf(user_message, sizeof(user_message), format, args);

    char timestamp[MAX_TIMESTAMP_LEN];
    int show_date = category->format ? category->format->show_date : 0;
    get_timestamp(timestamp, sizeof(timestamp), show_date);
    
    const char *level_str = log_level_to_string(level);
    const char *color = get_level_color(level);
    const char *reset_color = COLOR_RESET;
    
    // 1. Always log to file if category has file output configured
    if (category->output && category->output != stdout && category->output != stderr) {
        bool use_colors_for_file = should_use_colors_for_file(category);
        write_log_message_to_stream(category->output, category, timestamp, level_str, 
                                  color, reset_color, user_message, use_colors_for_file);
    }
    
    // 2. Additionally send to console streams based on log level
    FILE *console_output = NULL;
    if (level >= LOG_LEVEL_WARN) {
        // error and warn -> stderr
        console_output = stderr;
    } else if (level == LOG_LEVEL_NOTICE) {
        // notice -> stdout  
        console_output = stdout;
    }
    // info and debug -> log file only (no console output)
    
    if (console_output) {
        write_log_message_to_stream(console_output, category, timestamp, level_str,
                                  color, reset_color, user_message, colors_enabled);  // Use colors_enabled for console output
    }
    
    return LOG_OK;
}

/* Initialize logging system */
int log_init(const char *config) {
    if (log_initialized) {
        return LOG_OK;
    }
    
    // Auto-detect if we should enable colors (if output is a TTY)
    colors_enabled = isatty(fileno(stdout));
    
    // Set default category output if not already configured
    if (!default_category.output) {
        default_category.output = stdout;
    }
    if (!default_category.format) {
        default_category.format = &default_format;
    }
    
    // Initialize default formats if none exist
    if (formats_count == 0) {
        log_add_format("default", "%T %L %C %I%m%n");
        log_add_format("simple", "%T %L %C %I%m%n");
    }
    
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
void log_finish(void) {
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
    formats_count = 0;
    rules_count = 0;
    log_initialized = 0;
}

/* Reload configuration */
int log_reload(const char *config) {
    log_finish();
    return log_init(config);
}

/* Get or create a category */
log_category_t* log_get_category(const char *cname) {
    if (!cname || strcmp(cname, "default") == 0) {
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
        cat->format = &default_format;
        cat->output_filename[0] = '\0';  // Initialize empty filename
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

void log_default_finish(void) {
    log_finish();
    default_category.format = &default_format;
    log_default_category = &default_category;
}

/* Parse log level from string */
static int parse_log_level(const char *level_str) {
    if (!level_str) return LOG_LEVEL_DEBUG;
    
    if (strcasecmp(level_str, "FATAL") == 0) return LOG_LEVEL_FATAL;
    if (strcasecmp(level_str, "ERROR") == 0) return LOG_LEVEL_ERROR;
    if (strcasecmp(level_str, "WARN") == 0) return LOG_LEVEL_WARN;
    if (strcasecmp(level_str, "NOTICE") == 0) return LOG_LEVEL_NOTICE;
    if (strcasecmp(level_str, "INFO") == 0) return LOG_LEVEL_INFO;
    if (strcasecmp(level_str, "DEBUG") == 0) return LOG_LEVEL_DEBUG;
    
    return LOG_LEVEL_DEBUG;
}

/* Trim whitespace from string */
static char* trim_whitespace(char *str) {
    char *end;
    
    // Trim leading space
    while (*str == ' ' || *str == '\t') str++;
    
    if (*str == 0) return str;
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    
    end[1] = '\0';
    return str;
}

/* Main configuration parsing function */
int log_parse_config_string(const char *config) {
    if (!config) return LOG_OK;
    
    // Detect format: if it contains [sections], use zlog format, otherwise use simple format
    if (strstr(config, "[formats]") || strstr(config, "[rules]")) {
        return log_parse_zlog_config(config);
    } else {
        return log_parse_simple_config(config);
    }
}

/* Parse zlog-style configuration */
static int log_parse_zlog_config(const char *config) {
    if (!config) return LOG_OK;
    
    char *config_copy = strdup(config);
    if (!config_copy) return LOG_INIT_FAIL;
    
    char *line = strtok(config_copy, "\n");
    char current_section[64] = "";
    
    while (line) {
        line = trim_whitespace(line);
        
        // Skip empty lines and comments
        if (*line == '\0' || *line == '#') {
            line = strtok(NULL, "\n");
            continue;
        }
        
        // Parse section headers [section]
        if (*line == '[' && line[strlen(line) - 1] == ']') {
            strncpy(current_section, line + 1, sizeof(current_section) - 1);
            current_section[sizeof(current_section) - 1] = '\0';
            // Remove trailing ]
            char *close_bracket = strrchr(current_section, ']');
            if (close_bracket) *close_bracket = '\0';
            
            line = strtok(NULL, "\n");
            continue;
        }
        
        // Parse content based on current section
        if (strcmp(current_section, "formats") == 0) {
            // Format definition: name = pattern
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                char *name = trim_whitespace(line);
                char *pattern = trim_whitespace(eq + 1);
                
                // Remove quotes if present
                if (*pattern == '"' && pattern[strlen(pattern) - 1] == '"') {
                    pattern++;
                    pattern[strlen(pattern) - 1] = '\0';
                }
                
                log_add_format(name, pattern);
            }
        } else if (strcmp(current_section, "rules") == 0) {
            // Rule definition: category.LEVEL "output"; format
            char *semicolon = strchr(line, ';');
            char *format_name = "default";
            
            if (semicolon) {
                *semicolon = '\0';
                format_name = trim_whitespace(semicolon + 1);
            }
            
            // Parse category.LEVEL "output"
            char *dot = strchr(line, '.');
            if (dot) {
                *dot = '\0';
                char *category_name = trim_whitespace(line);
                char *rest = trim_whitespace(dot + 1);
                
                // Find the start of the output specification
                char *quote = strchr(rest, '"');
                char *level_end = quote ? quote : (rest + strlen(rest));
                
                // Extract level
                char level_str[32];
                size_t level_len = level_end - rest;
                if (level_len < sizeof(level_str)) {
                    strncpy(level_str, rest, level_len);
                    level_str[level_len] = '\0';
                    char *trimmed_level = trim_whitespace(level_str);
                    
                    int level = parse_log_level(trimmed_level);
                    
                    // Extract output file if present
                    char *output_file = NULL;
                    if (quote) {
                        char *end_quote = strchr(quote + 1, '"');
                        if (end_quote) {
                            *end_quote = '\0';
                            output_file = quote + 1;
                        }
                    }
                    
                    // Apply rule to category
                    log_category_t *cat = log_get_category(category_name);
                    if (cat) {
                        cat->level = level;
                        cat->format = log_get_format(format_name);
                        
                        if (output_file && strlen(output_file) > 0) {
                            // Clear the file first (for fresh start)
                            FILE *clear_file = fopen(output_file, "w");
                            if (clear_file) {
                                fclose(clear_file);
                            }
                            
                            // Now open in append mode for logging
                            FILE *file = fopen(output_file, "a");
                            if (file) {
                                // Close previous file if not stdout/stderr
                                if (cat->output && cat->output != stdout && cat->output != stderr) {
                                    fclose(cat->output);
                                }
                                cat->output = file;
                                // Store filename for color decision
                                strncpy(cat->output_filename, output_file, sizeof(cat->output_filename) - 1);
                                cat->output_filename[sizeof(cat->output_filename) - 1] = '\0';
                            }
                        }
                    }
                }
            }
        }
        
        line = strtok(NULL, "\n");
    }
    
    free(config_copy);
    return LOG_OK;
}

/* Simple configuration parsing for backwards compatibility */
static int log_parse_simple_config(const char *config) {
    if (!config) return LOG_OK;
    
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
                int level = parse_log_level(value);
                log_set_level(&default_category, level);
            } else if (strcmp(key, "format") == 0) {
                // Remove quotes if present
                if (*value == '"' && value[strlen(value) - 1] == '"') {
                    value++;
                    value[strlen(value) - 1] = '\0';
                }
                log_set_default_format(value);
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

/* Indentation management functions */
void log_set_indent(int indent) {
    if (indent < 0) {
        log_indent = 0;
    } else if (indent > LOG_MAX_INDENT_LEVEL * 2) {
        log_indent = LOG_MAX_INDENT_LEVEL * 2;
    } else {
        log_indent = indent;
    }
}

int log_get_indent(void) {
    return log_indent;
}

void log_reset_indent(void) {
    log_indent = 0;
}
