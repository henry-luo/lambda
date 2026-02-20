#include "transpiler.hpp"
#include "../lib/log.h"
#include "../lib/file.h"
#include "utf_string.h"
#include "format/format.h"

#include <stdarg.h>
#include <time.h>
#include <errno.h>  // for errno checking
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#include <direct.h>  // for _mkdir
#include <io.h>      // for _access
// Windows doesn't have these macros, provide simple equivalents
#define WIFEXITED(status) (1)
#define WEXITSTATUS(status) (status)
#else
#include <sys/wait.h>  // for WIFEXITED, WEXITSTATUS
#include <sys/stat.h>  // for mkdir, chmod
#include <unistd.h>    // for symlink, rmdir, access
#include <dirent.h>    // for opendir, readdir
#include <utime.h>     // for utime (touch)
#include <ftw.h>       // for nftw (recursive delete)
#endif
#include <string.h>  // for strlen
#include "input/input.hpp"

extern __thread EvalContext* context;

// Global dry-run flag: when true, IO operations return fabricated results
bool g_dry_run = false;

// ============================================================================
// Dry-run fabricated results
// These return realistic mock data so code paths that process IO results
// are still exercised during fuzzy testing.
// ============================================================================

// Fabricated JSON content for input() in dry-run mode
static const char* DRY_RUN_JSON = "{\"name\": \"dry-run\", \"version\": \"1.0\", \"items\": [1, 2, 3], \"active\": true}";
// Fabricated text content for file reads
static const char* DRY_RUN_TEXT = "Dry-run fabricated content.\nLine 2 of mock data.\nLine 3 with numbers: 42, 3.14\n";
// Fabricated HTML content
static const char* DRY_RUN_HTML = "<html><head><title>Mock</title></head><body><p>Dry-run content</p></body></html>";
// Fabricated HTTP response body
static const char* DRY_RUN_HTTP_BODY = "{\"status\": \"ok\", \"data\": {\"id\": 1, \"message\": \"dry-run response\"}, \"timestamp\": 1700000000}";
// Fabricated command output
static const char* DRY_RUN_CMD_OUTPUT = "dry-run-output";

static Item dry_run_fabricated_input(Item target_item, Item type) {
    log_debug("dry-run: fabricated input() call");

    // determine what kind of data to fabricate based on type hint or file extension
    const char* content = DRY_RUN_JSON;
    const char* type_hint = NULL;

    TypeId type_id = get_type_id(type);
    if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        String* ts = fn_string(type);
        if (ts) type_hint = ts->chars;
    }

    // choose content based on type hint
    if (type_hint) {
        if (strcmp(type_hint, "html") == 0) content = DRY_RUN_HTML;
        else if (strcmp(type_hint, "text") == 0 || strcmp(type_hint, "txt") == 0) content = DRY_RUN_TEXT;
        else if (strcmp(type_hint, "json") == 0) content = DRY_RUN_JSON;
        else if (strcmp(type_hint, "csv") == 0) content = "name,age,city\nAlice,30,NYC\nBob,25,LA\n";
        else if (strcmp(type_hint, "yaml") == 0 || strcmp(type_hint, "yml") == 0) content = "name: dry-run\nversion: 1\nitems:\n  - one\n  - two\n";
        else if (strcmp(type_hint, "xml") == 0) content = "<root><item id=\"1\">mock</item><item id=\"2\">data</item></root>";
        else if (strcmp(type_hint, "markdown") == 0 || strcmp(type_hint, "md") == 0) content = "# Mock\n\nDry-run content.\n\n- item 1\n- item 2\n";
        else if (strcmp(type_hint, "toml") == 0) content = "[package]\nname = \"mock\"\nversion = \"1.0\"\n";
        else if (strcmp(type_hint, "ini") == 0) content = "[section]\nkey1 = value1\nkey2 = value2\n";
    } else {
        // try to infer from file extension
        TypeId target_type_id = get_type_id(target_item);
        if (target_type_id == LMD_TYPE_STRING || target_type_id == LMD_TYPE_SYMBOL) {
            const char* path = target_item.get_chars();
            if (path) {
                const char* dot = strrchr(path, '.');
                if (dot) {
                    const char* ext = dot + 1;
                    if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0) content = DRY_RUN_HTML;
                    else if (strcmp(ext, "txt") == 0) content = DRY_RUN_TEXT;
                    else if (strcmp(ext, "csv") == 0) content = "name,age,city\nAlice,30,NYC\nBob,25,LA\n";
                    else if (strcmp(ext, "yaml") == 0 || strcmp(ext, "yml") == 0) content = "name: dry-run\nversion: 1\n";
                    else if (strcmp(ext, "xml") == 0) content = "<root><item>mock</item></root>";
                    else if (strcmp(ext, "md") == 0) content = "# Mock\n\nDry-run.\n";
                    // json is default anyway
                }
            }
        }
    }

    // parse the fabricated content through the actual input pipeline
    // so downstream code gets properly typed Lambda data structures
    String* content_str = heap_strcpy((char*)content, strlen(content));
    return {.item = s2it(content_str)};
}

static Item dry_run_fabricated_output() {
    log_debug("dry-run: fabricated output() call");
    // return fabricated bytes-written count
    return {.item = i2it(42)};
}

static Item dry_run_fabricated_fetch() {
    log_debug("dry-run: fabricated fetch() call");
    String* body = heap_strcpy((char*)DRY_RUN_HTTP_BODY, strlen(DRY_RUN_HTTP_BODY));
    return {.item = s2it(body)};
}

static Item dry_run_fabricated_cmd() {
    log_debug("dry-run: fabricated cmd() call");
    String* output = heap_strcpy((char*)DRY_RUN_CMD_OUTPUT, strlen(DRY_RUN_CMD_OUTPUT));
    return {.item = s2it(output)};
}

static Bool dry_run_fabricated_exists() {
    log_debug("dry-run: fabricated exists() call");
    return BOOL_FALSE;
}
// ============================================================================

Item pn_print(Item item) {
    TypeId type_id = get_type_id(item);
    log_debug("pn_print: %d", type_id);
    String *str = fn_string(item);
    if (str) {
        printf("%s", str->chars);
    }
    return ItemNull;
}

double pn_clock() {
    struct timespec ts;
#ifdef __APPLE__
    clock_gettime(CLOCK_MONOTONIC, &ts);
#elif defined(_WIN32)
    // Windows: use QueryPerformanceCounter via timespec_get
    timespec_get(&ts, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

// Helper: Create parent directories recursively for a file path
static int create_parent_dirs(const char* file_path) {
    char* path_copy = strdup(file_path);
    if (!path_copy) return -1;

    // Find last path separator to get directory portion
    char* last_sep = strrchr(path_copy, '/');
#ifdef _WIN32
    char* last_sep_win = strrchr(path_copy, '\\');
    if (last_sep_win > last_sep) last_sep = last_sep_win;
#endif

    if (!last_sep || last_sep == path_copy) {
        // no directory component or just root
        free(path_copy);
        return 0;
    }

    *last_sep = '\0';  // truncate to get directory path

#ifdef _WIN32
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "mkdir \"%s\" 2>nul", path_copy);
    int result = system(cmd);
    if (result != 0) {
        DWORD attr = GetFileAttributesA(path_copy);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            result = 0;
        }
    }
#else
    char* p = path_copy;
    int result = 0;

    if (*p == '/') p++;

    while (*p) {
        while (*p && *p != '/') p++;
        char saved = *p;
        *p = '\0';

        if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
            result = -1;
            break;
        }

        *p = saved;
        if (saved) p++;
    }
#endif

    free(path_copy);
    return result;
}

// Helper: Generate a unique temp file path for atomic writes
static void generate_temp_path(const char* file_path, StrBuf* temp_buf) {
    strbuf_append_str(temp_buf, file_path);
    strbuf_append_str(temp_buf, ".tmp.");
    // append pid and timestamp for uniqueness
#ifdef _WIN32
    strbuf_append_int(temp_buf, (int)_getpid());
#else
    strbuf_append_int(temp_buf, (int)getpid());
#endif
    strbuf_append_char(temp_buf, '.');
    strbuf_append_int(temp_buf, (int)time(NULL));
}

// Helper: Perform atomic rename (temp file to final file)
static int atomic_rename(const char* temp_path, const char* final_path) {
#ifdef _WIN32
    // Windows: MoveFileEx with MOVEFILE_REPLACE_EXISTING
    if (!MoveFileExA(temp_path, final_path, MOVEFILE_REPLACE_EXISTING)) {
        return -1;
    }
    return 0;
#else
    // POSIX: rename() is atomic on the same filesystem
    return rename(temp_path, final_path);
#endif
}

// Unified output implementation
// source: data to output
// target_item: file path (String, Symbol, or Path)
// format_str: optional format type ('json, 'yaml, 'html, etc.), NULL for auto-detect
// append: true for append mode, false for write (truncate) mode
// atomic: if true, write to temp file first then rename (only for write mode, not append)
// Returns: bytes written on success, ItemError on failure
static Item pn_output_internal(Item source, Item target_item, const char* format_str, bool append, bool atomic) {
    if (g_dry_run) return dry_run_fabricated_output();
    const char* mode = append ? "a" : "w";
    const char* mode_binary = append ? "ab" : "wb";

    // atomic writes only make sense for write mode (not append)
    if (atomic && append) {
        log_debug("pn_output_internal: atomic mode ignored for append");
        atomic = false;
    }

    // Validate target type: must be string, symbol, or path
    TypeId target_type = get_type_id(target_item);
    if (target_type != LMD_TYPE_STRING && target_type != LMD_TYPE_SYMBOL && target_type != LMD_TYPE_PATH) {
        log_error("pn_output_internal: target must be string, symbol, or path, got type %s", get_type_name(target_type));
        return ItemError;
    }

    // Convert target to unified Target struct
    Url* cwd = context ? (Url*)context->cwd : NULL;
    Target* target = item_to_target(target_item.item, cwd);
    if (!target) {
        log_error("pn_output_internal: failed to convert item to target");
        return ItemError;
    }

    // Check if target is writable (local file only)
    if (!target_is_local(target)) {
        log_error("pn_output_internal: cannot write to remote URL (scheme=%d)", target->scheme);
        target_free(target);
        return ItemError;
    }

    // Get local file path from target
    StrBuf* path_buf = (StrBuf*)target_to_local_path(target, cwd);
    if (!path_buf || !path_buf->str || path_buf->length == 0) {
        log_error("pn_output_internal: failed to resolve target to local path");
        target_free(target);
        if (path_buf) strbuf_free(path_buf);
        return ItemError;
    }

    const char* file_path = path_buf->str;
    log_debug("pn_output_internal: writing to %s (mode=%s, format=%s)", file_path, mode, format_str ? format_str : "auto");

    // Always try to create parent directories for resolved paths
    if (create_parent_dirs(file_path) != 0) {
        log_error("pn_output_internal: failed to create directories for %s", file_path);
        strbuf_free(path_buf);
        target_free(target);
        return ItemError;
    }

    // Clean up target (no longer needed)
    target_free(target);

    // handle error source: report and return error
    TypeId source_type = get_type_id(source);
    if (source_type == LMD_TYPE_ERROR) {
        log_error("pn_output_internal: cannot output error to file");
        strbuf_free(path_buf);
        return ItemError;
    }

    // string source: output as raw text (ignore format)
    if (source_type == LMD_TYPE_STRING) {
        String* str = it2s(source);
        if (!str) {
            log_error("pn_output_internal: source string is null");
            strbuf_free(path_buf);
            return ItemError;
        }

        // determine actual write path (temp file for atomic writes)
        StrBuf* temp_path_buf = NULL;
        const char* write_path = file_path;
        if (atomic) {
            temp_path_buf = strbuf_new();
            generate_temp_path(file_path, temp_path_buf);
            write_path = temp_path_buf->str;
        }

        FILE* f = fopen(write_path, mode);
        if (!f) {
            log_error("pn_output_internal: failed to open file %s: %s", write_path, strerror(errno));
            if (temp_path_buf) strbuf_free(temp_path_buf);
            strbuf_free(path_buf);
            return ItemError;
        }

        size_t written = fwrite(str->chars, 1, str->len, f);
        fclose(f);

        if (written != (size_t)str->len) {
            log_error("pn_output_internal: failed to write to file %s", write_path);
            if (atomic) remove(write_path);  // clean up temp file on error
            if (temp_path_buf) strbuf_free(temp_path_buf);
            strbuf_free(path_buf);
            return ItemError;
        }

        // atomic: rename temp file to final path
        if (atomic) {
            if (atomic_rename(write_path, file_path) != 0) {
                log_error("pn_output_internal: atomic rename failed: %s -> %s: %s", write_path, file_path, strerror(errno));
                remove(write_path);  // clean up temp file
                strbuf_free(temp_path_buf);
                strbuf_free(path_buf);
                return ItemError;
            }
            strbuf_free(temp_path_buf);
        }

        log_debug("pn_output_internal: wrote %zu bytes (text) to %s", written, file_path);
        strbuf_free(path_buf);
        return {.item = i2it((int64_t)written)};
    }

    // binary source: output as raw binary data (ignore format)
    if (source_type == LMD_TYPE_BINARY) {
        Binary* bin = (Binary*)it2s(source);
        if (!bin) {
            log_error("pn_output_internal: source binary is null");
            strbuf_free(path_buf);
            return ItemError;
        }

        // determine actual write path (temp file for atomic writes)
        StrBuf* temp_path_buf = NULL;
        const char* write_path = file_path;
        if (atomic) {
            temp_path_buf = strbuf_new();
            generate_temp_path(file_path, temp_path_buf);
            write_path = temp_path_buf->str;
        }

        FILE* f = fopen(write_path, mode_binary);
        if (!f) {
            log_error("pn_output_internal: failed to open file %s: %s", write_path, strerror(errno));
            if (temp_path_buf) strbuf_free(temp_path_buf);
            strbuf_free(path_buf);
            return ItemError;
        }

        size_t written = fwrite(bin->chars, 1, bin->len, f);
        fclose(f);

        if (written != (size_t)bin->len) {
            log_error("pn_output_internal: failed to write to file %s", write_path);
            if (atomic) remove(write_path);  // clean up temp file on error
            if (temp_path_buf) strbuf_free(temp_path_buf);
            strbuf_free(path_buf);
            return ItemError;
        }

        // atomic: rename temp file to final path
        if (atomic) {
            if (atomic_rename(write_path, file_path) != 0) {
                log_error("pn_output_internal: atomic rename failed: %s -> %s: %s", write_path, file_path, strerror(errno));
                remove(write_path);  // clean up temp file
                strbuf_free(temp_path_buf);
                strbuf_free(path_buf);
                return ItemError;
            }
            strbuf_free(temp_path_buf);
        }

        log_debug("pn_output_internal: wrote %zu bytes (binary) to %s", written, file_path);
        strbuf_free(path_buf);
        return {.item = i2it((int64_t)written)};
    }

    // determine format for structured data
    const char* effective_format = format_str;
    if (!effective_format) {
        // auto-detect format from file extension
        const char* dot = strrchr(file_path, '.');
        if (dot) {
            const char* ext = dot + 1;
            if (strcmp(ext, "json") == 0) effective_format = "json";
            else if (strcmp(ext, "yaml") == 0 || strcmp(ext, "yml") == 0) effective_format = "yaml";
            else if (strcmp(ext, "xml") == 0) effective_format = "xml";
            else if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0) effective_format = "html";
            else if (strcmp(ext, "md") == 0) effective_format = "markdown";
            else if (strcmp(ext, "csv") == 0) effective_format = "csv";
            else if (strcmp(ext, "txt") == 0) effective_format = "text";
            else if (strcmp(ext, "toml") == 0) effective_format = "toml";
            else if (strcmp(ext, "ini") == 0) effective_format = "ini";
            else if (strcmp(ext, "ls") == 0 || strcmp(ext, "mark") == 0 || strcmp(ext, "mk") == 0) effective_format = "mark";
            // unknown extension: leave as NULL for fallback handling below
        }
        // no extension or unknown extension: effective_format remains NULL
    }

    // fallback when format not detected: based on data type
    if (!effective_format) {
        if (source_type == LMD_TYPE_STRING) {
            effective_format = "text";
        } else if (source_type == LMD_TYPE_BINARY) {
            effective_format = "binary";
        } else {
            effective_format = "mark";  // default to mark for structured data
        }
    }

    log_debug("pn_output_internal: using format '%s'", effective_format);

    // format the data
    Pool* temp_pool = pool_create();
    String* formatted = NULL;
    bool use_mark_format = false;

    if (strcmp(effective_format, "json") == 0) {
        formatted = format_json(temp_pool, source);
    } else if (strcmp(effective_format, "yaml") == 0) {
        formatted = format_yaml(temp_pool, source);
    } else if (strcmp(effective_format, "xml") == 0) {
        formatted = format_xml(temp_pool, source);
    } else if (strcmp(effective_format, "html") == 0) {
        formatted = format_html(temp_pool, source);
    } else if (strcmp(effective_format, "markdown") == 0) {
        StringBuf* sb = stringbuf_new(temp_pool);
        format_markdown(sb, source);
        formatted = stringbuf_to_string(sb);
        stringbuf_free(sb);
    } else if (strcmp(effective_format, "text") == 0) {
        formatted = format_text_string(temp_pool, source);
    } else if (strcmp(effective_format, "toml") == 0) {
        formatted = format_toml(temp_pool, source);
    } else if (strcmp(effective_format, "ini") == 0) {
        formatted = format_ini(temp_pool, source);
    } else if (strcmp(effective_format, "mark") == 0) {
        use_mark_format = true;
    } else {
        log_error("pn_output_internal: unsupported format '%s'", effective_format);
        pool_destroy(temp_pool);
        strbuf_free(path_buf);
        return ItemError;
    }

    // determine actual write path (temp file for atomic writes)
    StrBuf* temp_path_buf = NULL;
    const char* write_path = file_path;
    if (atomic) {
        temp_path_buf = strbuf_new();
        generate_temp_path(file_path, temp_path_buf);
        write_path = temp_path_buf->str;
    }

    // write to file
    FILE* f = fopen(write_path, mode);
    if (!f) {
        log_error("pn_output_internal: failed to open file %s: %s", write_path, strerror(errno));
        pool_destroy(temp_pool);
        if (temp_path_buf) strbuf_free(temp_path_buf);
        strbuf_free(path_buf);
        return ItemError;
    }

    size_t written;
    if (use_mark_format) {
        // use print_item for native Mark format
        StrBuf* content_buf = strbuf_new_cap(1024);
        print_item(content_buf, source, 0, NULL);
        strbuf_append_char(content_buf, '\n');  // add trailing newline
        written = fwrite(content_buf->str, 1, content_buf->length, f);
        fclose(f);

        if (written != content_buf->length) {
            log_error("pn_output_internal: failed to write to file %s", write_path);
            if (atomic) remove(write_path);  // clean up temp file on error
            strbuf_free(content_buf);
            pool_destroy(temp_pool);
            if (temp_path_buf) strbuf_free(temp_path_buf);
            strbuf_free(path_buf);
            return ItemError;
        }

        log_debug("pn_output_internal: wrote %zu bytes (mark) to %s", written, file_path);
        strbuf_free(content_buf);
    } else {
        if (!formatted || !formatted->chars) {
            log_error("pn_output_internal: formatting failed");
            fclose(f);
            pool_destroy(temp_pool);
            if (temp_path_buf) strbuf_free(temp_path_buf);
            strbuf_free(path_buf);
            return ItemError;
        }

        written = fwrite(formatted->chars, 1, strlen(formatted->chars), f);
        fclose(f);

        if (written != strlen(formatted->chars)) {
            log_error("pn_output_internal: failed to write to file %s", write_path);
            if (atomic) remove(write_path);  // clean up temp file on error
            pool_destroy(temp_pool);
            if (temp_path_buf) strbuf_free(temp_path_buf);
            strbuf_free(path_buf);
            return ItemError;
        }

        log_debug("pn_output_internal: wrote %zu bytes (%s) to %s", written, effective_format, file_path);
    }

    // atomic: rename temp file to final path
    if (atomic) {
        if (atomic_rename(write_path, file_path) != 0) {
            log_error("pn_output_internal: atomic rename failed: %s -> %s: %s", write_path, file_path, strerror(errno));
            remove(write_path);  // clean up temp file
            pool_destroy(temp_pool);
            strbuf_free(temp_path_buf);
            strbuf_free(path_buf);
            return ItemError;
        }
        strbuf_free(temp_path_buf);
    }

    pool_destroy(temp_pool);
    strbuf_free(path_buf);
    return {.item = i2it((int64_t)written)};
}

// 2-parameter wrapper: output(source, trg) - writes data to target (default format, write mode)
// Returns: bytes written on success, error on failure
Item pn_output2(Item source, Item target_item) {
    return pn_output_internal(source, target_item, NULL, false, false);
}

// Helper: Get string from map field (returns NULL if not found or wrong type)
static const char* get_map_string_field(Map* map, const char* field_name) {
    if (!map) return NULL;

    String* key = heap_strcpy((char*)field_name, strlen(field_name));
    Item key_item = {.item = s2it(key)};
    Item value = map_get(map, key_item);

    TypeId value_type = get_type_id(value);
    if (value_type == LMD_TYPE_SYMBOL || value_type == LMD_TYPE_STRING) {
        const char* chars = value.get_chars();
        if (chars) {
            return chars;
        }
    }
    return NULL;
}

// Helper: Get boolean from map field (returns default_val if not found or wrong type)
static bool get_map_bool_field(Map* map, const char* field_name, bool default_val) {
    if (!map) return default_val;

    String* key = heap_strcpy((char*)field_name, strlen(field_name));
    Item key_item = {.item = s2it(key)};
    Item value = map_get(map, key_item);

    TypeId value_type = get_type_id(value);
    if (value_type == LMD_TYPE_BOOL) {
        return value.bool_val;
    }
    return default_val;
}

// 3-parameter wrapper: output(source, target, options) - with options map, symbol, or string
// If 3rd arg is a symbol or string, it is treated as the output format.
// If 3rd arg is a map, it supports:
//   format: symbol - output format ('json, 'yaml, 'xml, 'html, 'markdown, 'text, 'mark, etc.)
//   mode: symbol - 'write or 'append (default: 'write)
//   atomic: bool - write to temp file first, then rename (default: false)
// Returns: bytes written on success, error on failure
Item pn_output3(Item source, Item target_item, Item options_item) {
    const char* format_str = NULL;
    bool append = false;
    bool atomic = false;

    TypeId options_type = get_type_id(options_item);
    if (options_type == LMD_TYPE_MAP) {
        Map* options = options_item.map;

        // extract format option
        format_str = get_map_string_field(options, "format");

        // extract mode option
        const char* mode_str = get_map_string_field(options, "mode");
        if (mode_str && strcmp(mode_str, "append") == 0) {
            append = true;
        }

        // extract atomic option
        atomic = get_map_bool_field(options, "atomic", false);
    } else if (options_type == LMD_TYPE_SYMBOL || options_type == LMD_TYPE_STRING) {
        // treat symbol or string as the format
        const char* fmt_chars = options_item.get_chars();
        uint32_t fmt_len = options_item.get_len();
        if (fmt_chars && fmt_len > 0) {
            format_str = fmt_chars;
        }
    } else if (options_type != LMD_TYPE_NULL) {
        log_error("pn_output3: options must be a map, symbol, string, or null, got type %s", get_type_name(options_type));
        return ItemError;
    }

    return pn_output_internal(source, target_item, format_str, append, atomic);
}

// 2-parameter append wrapper: used by |>> pipe operator
// Directly calls pn_output_internal with append=true, no format, no atomic
Item pn_output_append(Item source, Item target_item) {
    return pn_output_internal(source, target_item, NULL, true, false);
}

extern void free_fetch_response(FetchResponse* response);

// Convert FetchResponse to Lambda Item (simplified approach)
Item fetch_response_to_item(FetchResponse* response) {
    if (!response) { return ItemError; }
    Item result;
    // For now, return a simple string with the response data
    // TODO: Implement proper map structure when the complex type system is working
    String* result_str;
    if (response->data && response->size > 0) {
        result_str = heap_strcpy(response->data, response->size);
        result = {.item = s2it(result_str)};
    } else {
        result = ItemNull;
    }
    // Clean up the response structure
    free_fetch_response(response);
    return result;
}

Item pn_fetch(Item url, Item options) {
    if (g_dry_run) return dry_run_fabricated_fetch();
    log_debug("pn_fetch called");
    // Validate URL parameter
    String* url_str;
    if (url._type_id != LMD_TYPE_STRING && url._type_id != LMD_TYPE_SYMBOL) {
        log_debug("fetch url must be a string or symbol, got type %s", get_type_name(url._type_id));
        return ItemError;
    }
    url_str = fn_string(url);

    // Parse options parameter (similar to JS fetch options)
    FetchConfig config = {
        .method = "GET",
        .body = NULL,
        .body_size = 0,
        .headers = NULL,
        .header_count = 0,
        .timeout_seconds = 30,
        .max_redirects = 5,
        .user_agent = "Lambda/0.1",
        .verify_ssl = true,
        .enable_compression = true
    };

    // Helper to create Item from string literal
    auto create_string_item = [](const char* str) -> Item {
        String* string = heap_strcpy((char*)str, strlen(str));
        return (Item){.item = s2it(string)};
    };

    // Parse options if provided
    TypeId options_type = get_type_id(options);
    if (options_type == LMD_TYPE_MAP) {
        Map* options_map = options.map;

        // Extract method
        Item method_key = create_string_item("method");
        Item method_item = map_get(options_map, method_key);
        if (method_item.item && (method_item._type_id == LMD_TYPE_STRING || method_item._type_id == LMD_TYPE_SYMBOL)) {
            config.method = method_item.get_chars();
        }

        // Extract body
        Item body_key = create_string_item("body");
        Item body_item = map_get(options_map, body_key);
        if (body_item.item) {
            if (body_item._type_id == LMD_TYPE_STRING || body_item._type_id == LMD_TYPE_SYMBOL) {
                config.body = body_item.get_chars();
                config.body_size = body_item.get_len();
            } else {
                // Convert other types to string representation
                String* body_str = fn_string(body_item);
                if (body_str) {
                    config.body = body_str->chars;
                    config.body_size = body_str->len;
                }
            }
        }

        // Extract headers
        Item headers_key = create_string_item("headers");
        Item headers_item = map_get(options_map, headers_key);
        if (headers_item.type_id() == LMD_TYPE_MAP) {
            // Convert headers map to curl header list
            // For now, we'll allocate a simple array (this could be enhanced)
            log_debug("fetch: headers map found but not fully implemented yet");
        }

        // Extract timeout
        Item timeout_key = create_string_item("timeout");
        Item timeout_item = map_get(options_map, timeout_key);
        if (timeout_item.item && (timeout_item._type_id == LMD_TYPE_INT || timeout_item._type_id == LMD_TYPE_INT64)) {
            config.timeout_seconds = it2l(timeout_item);
        }
    }
    else if (options_type != LMD_TYPE_NULL) {
        log_debug("fetch options must be a map or null, got type %s", get_type_name(options_type));
        // Continue with default config
    }

    // Perform the HTTP request
    FetchResponse* response = http_fetch(url_str->chars, &config);
    if (!response) {
        log_debug("fetch: HTTP request failed");
        return ItemError;
    }

    // Convert response to Lambda Item
    return fetch_response_to_item(response);
}

// Helper function to escape shell arguments for safety
String* escape_shell_arg(String* arg) {
    if (!arg || arg->len == 0) {
        return heap_strcpy("''", 2);  // Empty string as quoted empty
    }

    // Check if argument needs escaping (contains spaces, special chars, etc.)
    bool needs_quoting = false;
    for (int i = 0; i < arg->len; i++) {
        char c = arg->chars[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '"' || c == '\'' ||
            c == '\\' || c == '|' || c == '&' || c == ';' || c == '(' || c == ')' ||
            c == '<' || c == '>' || c == '`' || c == '$' || c == '*' || c == '?' ||
            c == '[' || c == ']' || c == '{' || c == '}' || c == '~') {
            needs_quoting = true;
            break;
        }
    }

    if (!needs_quoting) {
        // Return original string if no escaping needed
        return arg;
    }

    // Use single quotes for safety and escape any single quotes in the string
    size_t escaped_len = arg->len + 2; // Start with quotes
    for (int i = 0; i < arg->len; i++) {
        if (arg->chars[i] == '\'') {
            escaped_len += 3; // Replace ' with '\''
        }
    }

    String* escaped = (String*)heap_alloc(sizeof(String) + escaped_len + 1, LMD_TYPE_STRING);
    escaped->len = escaped_len;

    char* dst = escaped->chars;
    *dst++ = '\''; // Opening quote

    for (int i = 0; i < arg->len; i++) {
        if (arg->chars[i] == '\'') {
            // Escape single quote: ' becomes '\''
            *dst++ = '\'';
            *dst++ = '\\';
            *dst++ = '\'';
            *dst++ = '\'';
        } else {
            *dst++ = arg->chars[i];
        }
    }

    *dst++ = '\''; // Closing quote
    *dst = '\0';

    return escaped;
}

// Helper function to format arguments into command line string
String* format_cmd_args(String* cmd, Item args) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, cmd->chars);

    TypeId args_type = get_type_id(args);

    if (args_type == LMD_TYPE_NULL) {
        // No arguments
    }
    else if (args_type == LMD_TYPE_LIST || args_type == LMD_TYPE_ARRAY) {
        List* arg_list = args.list;
        for (int i = 0; i < arg_list->length; i++) {
            Item arg_item = arg_list->items[i];
            String* arg_str = fn_string(arg_item);
            if (arg_str && arg_str->len > 0) {
                String* escaped = escape_shell_arg(arg_str);
                strbuf_append_char(sb, ' ');
                strbuf_append_str(sb, escaped->chars);
            }
        }
    }
    else if (args_type == LMD_TYPE_MAP) {
        // Handle map-style arguments for named parameters
        Map* arg_map = args.map;
        TypeMap* type_map = (TypeMap*)arg_map->type;

        // Iterate through map entries using ShapeEntry linked list
        ShapeEntry* field = type_map->shape;
        for (int i = 0; i < type_map->length && field; i++) {
            if (!field->name || !field->name->str) {
                field = field->next;
                continue;
            }

            // Get field value by reconstructing the item from the field data
            void* field_ptr = (char*)arg_map->data + field->byte_offset;
            Item value_item = {.item = 0};

            switch (field->type->type_id) {
            case LMD_TYPE_NULL:
                value_item._type_id = LMD_TYPE_NULL;
                break;
            case LMD_TYPE_BOOL:
                value_item._type_id = LMD_TYPE_BOOL;
                value_item.bool_val = *(bool*)field_ptr;
                break;
            case LMD_TYPE_INT:
                value_item = {.item = i2it(*(int64_t*)field_ptr)};  // read full int64 to preserve 56-bit value
                break;
            case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL: {
                String* str = *(String**)field_ptr;
                value_item = {.item = s2it(str)};
                break;
            }
            default:
                value_item = ItemNull;
                break;
            }

            // format as --key=value or --key value depending on convention
            strbuf_append_str(sb, " --");
            strbuf_append_str_n(sb, field->name->str, field->name->length);

            String* value_str = fn_string(value_item);
            if (value_str && value_str->len > 0) {
                if (!(value_item._type_id == LMD_TYPE_BOOL && value_item.bool_val == true)) {
                    String* escaped = escape_shell_arg(value_str);
                    strbuf_append_char(sb, '=');
                    strbuf_append_str(sb, escaped->chars);
                }
                // else skip boolean true values (just add the flag)
            }

            field = field->next;
        }
    }
    else {
        // Single argument - convert to string
        String* arg_str = fn_string(args);
        if (arg_str && arg_str->len > 0) {
            String* escaped = escape_shell_arg(arg_str);
            strbuf_append_char(sb, ' ');
            strbuf_append_str(sb, escaped->chars);
        }
    }

    String* result = heap_strcpy(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

Item pn_cmd2(Item cmd, Item args);

Item pn_cmd1(Item cmd) {
    if (g_dry_run) return dry_run_fabricated_cmd();
    return pn_cmd2(cmd, ItemNull);
}

Item pn_cmd2(Item cmd, Item args) {
    if (g_dry_run) return dry_run_fabricated_cmd();
    log_debug("pn_cmd called");
    if (get_type_id(cmd) != LMD_TYPE_STRING) {
        log_debug("pn_cmd: command must be a string");
        return ItemError;
    }

    String* cmd_str = cmd.get_string();
    if (!cmd_str || cmd_str->len == 0) {
        log_debug("pn_cmd: command string is empty");
        return ItemError;
    }

    // Format the complete command with arguments
    String* full_cmd = format_cmd_args(cmd_str, args);
    if (!full_cmd) {
        log_debug("pn_cmd: failed to format command arguments");
        return ItemError;
    }

    log_debug("pn_cmd: executing command: %s", full_cmd->chars);

    // Use popen to capture stdout
    FILE* pipe = popen(full_cmd->chars, "r");
    if (!pipe) {
        log_error("pn_cmd: failed to execute command: %s (errno: %d)", full_cmd->chars, errno);
        return ItemError;
    }

    // Read output into a string buffer
    StrBuf* output_buf = strbuf_new();
    char buffer[4096];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        strbuf_append_str_n(output_buf, buffer, bytes_read);
    }

    // Get the exit status
    int exit_status = pclose(pipe);

    // Check for command execution errors
    if (exit_status == -1) {
        log_error("pn_cmd: pclose failed (errno: %d)", errno);
        strbuf_free(output_buf);
        return ItemError;
    }

    // Extract actual exit code (pclose returns status in wait() format)
    int actual_exit_code = WIFEXITED(exit_status) ? WEXITSTATUS(exit_status) : -1;
    log_debug("pn_cmd: command completed with exit code: %d", actual_exit_code);

    // Create result string from captured output
    String* result_str = NULL;
    Item result;
    if (output_buf->length > 0) {
        // trim trailing newline from command output
        size_t len = output_buf->length;
        while (len > 0 && (output_buf->str[len - 1] == '\n' || output_buf->str[len - 1] == '\r')) {
            len--;
        }
        result_str = heap_strcpy(output_buf->str, len);
        result = {.item = s2it(result_str)};
    } else {
        // return empty string for no output
        result_str = heap_strcpy("", 0);
        result = {.item = s2it(result_str)};
    }

    strbuf_free(output_buf);

    // non-zero exit code means the command failed — return error
    if (actual_exit_code != 0) {
        log_debug("pn_cmd: command failed with exit code %d", actual_exit_code);
        return ItemError;
    }

    log_debug("pn_cmd: command output: %s", result_str->chars);
    return result;
}

// ============================================================================
// I/O Module Functions (io module)
// These are procedural functions for unified I/O operations
// Supports both local file system and remote URLs
// ============================================================================

// Helper to extract local path from Item using unified Target API
// Accepts: Path, String, Symbol
// Returns StrBuf* that caller must free with strbuf_free()
// Returns NULL if item cannot be converted to local path (e.g., http:// URLs)
static StrBuf* get_local_path_from_item(Item item) {
    Target* target = item_to_target(item.item, NULL);
    if (!target) {
        return NULL;
    }

    // Check if local - some operations only work on local files
    if (!target_is_local(target)) {
        log_error("io: cannot perform operation on remote URL");
        target_free(target);
        return NULL;
    }

    StrBuf* path_buf = (StrBuf*)target_to_local_path(target, NULL);
    target_free(target);
    return path_buf;
}

#ifndef _WIN32
// Callback for nftw to remove files/directories recursively
static int remove_callback(const char* path, const struct stat* sb, int typeflag, struct FTW* ftwbuf) {
    (void)sb; (void)ftwbuf;
    int rv;
    if (typeflag == FTW_D || typeflag == FTW_DP) {
        rv = rmdir(path);
    } else {
        rv = remove(path);
    }
    if (rv != 0) {
        log_error("io.delete: failed to remove %s: %s", path, strerror(errno));
    }
    return rv;
}
#endif

// io.copy(src, dst) - Copy file or directory
// src can be local path or remote URL (http/https)
// dst must be a local path
Item pn_io_copy(Item src_item, Item dst_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.copy()"); return ItemNull; }
    // First, check if source is remote
    Target* src_target = item_to_target(src_item.item, NULL);
    if (!src_target) {
        log_error("io.copy: invalid source argument");
        return ItemError;
    }

    // Destination must always be local
    StrBuf* dst_buf = get_local_path_from_item(dst_item);
    if (!dst_buf) {
        log_error("io.copy: destination must be a local path");
        target_free(src_target);
        return ItemError;
    }

    const char* dst_path = dst_buf->str;

    // Handle remote source (fetch and save)
    if (target_is_remote(src_target)) {
        log_debug("io.copy: fetching remote source to %s", dst_path);

        // Get the URL string
        StrBuf* url_buf = strbuf_new();
        target_to_url_string(src_target, url_buf);
        String* url_str = heap_strcpy(url_buf->str, url_buf->length);
        strbuf_free(url_buf);
        target_free(src_target);

        // Fetch the remote data using pn_fetch
        Item url_item = {.item = s2it(url_str)};
        Item fetch_result = pn_fetch(url_item, ItemNull);

        if (fetch_result.item == ItemError.item || fetch_result.item == ItemNull.item) {
            log_error("io.copy: failed to fetch remote source");
            strbuf_free(dst_buf);
            return ItemError;
        }

        // Create parent directories if needed
        if (create_parent_dirs(dst_path) != 0) {
            log_error("io.copy: failed to create parent directories for %s", dst_path);
            strbuf_free(dst_buf);
            return ItemError;
        }

        // Write the fetched data to destination
        TypeId result_type = get_type_id(fetch_result);
        FILE* f = fopen(dst_path, "wb");
        if (!f) {
            log_error("io.copy: failed to open destination file %s: %s", dst_path, strerror(errno));
            strbuf_free(dst_buf);
            return ItemError;
        }

        size_t written = 0;
        if (result_type == LMD_TYPE_STRING) {
            String* str = it2s(fetch_result);
            if (str && str->chars) {
                written = fwrite(str->chars, 1, str->len, f);
            }
        } else if (result_type == LMD_TYPE_BINARY) {
            Binary* bin = (Binary*)it2s(fetch_result);
            if (bin && bin->chars) {
                written = fwrite(bin->chars, 1, bin->len, f);
            }
        }
        fclose(f);

        log_debug("io.copy: wrote %zu bytes from remote source to %s", written, dst_path);
        strbuf_free(dst_buf);
        return ItemNull;
    }

    // Local source - get local path
    StrBuf* src_buf = (StrBuf*)target_to_local_path(src_target, NULL);
    target_free(src_target);

    if (!src_buf) {
        log_error("io.copy: invalid source path");
        strbuf_free(dst_buf);
        return ItemError;
    }

    const char* src_path = src_buf->str;

    log_debug("io.copy: %s -> %s", src_path, dst_path);

    // Use platform command for simplicity (handles directories too)
#ifdef _WIN32
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "copy /Y \"%s\" \"%s\" >nul 2>&1 || xcopy /E /I /Y \"%s\" \"%s\" >nul 2>&1",
             src_path, dst_path, src_path, dst_path);
#else
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "cp -r '%s' '%s'", src_path, dst_path);
#endif

    int result = system(cmd);

    if (result != 0) {
        log_error("io.copy: failed to copy %s to %s", src_path, dst_path);
    }

    strbuf_free(src_buf);
    strbuf_free(dst_buf);

    return (result != 0) ? ItemError : ItemNull;
}

// io.move(src, dst) - Move/rename file or directory
Item pn_io_move(Item src_item, Item dst_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.move()"); return ItemNull; }
    StrBuf* src_buf = get_local_path_from_item(src_item);
    StrBuf* dst_buf = get_local_path_from_item(dst_item);

    if (!src_buf || !dst_buf) {
        log_error("io.move: invalid path argument");
        if (src_buf) strbuf_free(src_buf);
        if (dst_buf) strbuf_free(dst_buf);
        return ItemError;
    }

    const char* src_path = src_buf->str;
    const char* dst_path = dst_buf->str;

    log_debug("io.move: %s -> %s", src_path, dst_path);

    int result = rename(src_path, dst_path);

    // If rename fails (cross-device), fall back to copy+delete
    if (result != 0 && errno == EXDEV) {
        log_debug("io.move: cross-device move, using copy+delete");
        Item copy_result = pn_io_copy(src_item, dst_item);
        if (copy_result.item == ItemError.item) {
            strbuf_free(src_buf);
            strbuf_free(dst_buf);
            return ItemError;
        }
        // Delete source after successful copy
#ifdef _WIN32
        result = _unlink(src_path);
        if (result != 0) {
            // Try as directory
            result = _rmdir(src_path);
        }
#else
        result = remove(src_path);
        if (result != 0) {
            // Try as directory with recursive delete
            result = nftw(src_path, remove_callback, 64, FTW_DEPTH | FTW_PHYS);
        }
#endif
    }

    if (result != 0) {
        log_error("io.move: failed to move %s to %s: %s", src_path, dst_path, strerror(errno));
    }

    strbuf_free(src_buf);
    strbuf_free(dst_buf);

    return (result != 0) ? ItemError : ItemNull;
}

// io.delete(path) - Delete file or directory
Item pn_io_delete(Item path_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.delete()"); return ItemNull; }
    StrBuf* path_buf = get_local_path_from_item(path_item);

    if (!path_buf) {
        log_error("io.delete: invalid path argument");
        return ItemError;
    }

    const char* path = path_buf->str;

    log_debug("io.delete: %s", path);

    struct stat st;
    if (stat(path, &st) != 0) {
        log_error("io.delete: path does not exist: %s", path);
        strbuf_free(path_buf);
        return ItemError;
    }

    int result;
    if (S_ISDIR(st.st_mode)) {
        // Directory - use recursive delete
#ifdef _WIN32
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "rmdir /S /Q \"%s\"", path);
        result = system(cmd);
#else
        result = nftw(path, remove_callback, 64, FTW_DEPTH | FTW_PHYS);
#endif
    } else {
        // File - simple remove
        result = remove(path);
    }

    if (result != 0) {
        log_error("io.delete: failed to delete %s: %s", path, strerror(errno));
    }

    strbuf_free(path_buf);

    return (result != 0) ? ItemError : ItemNull;
}

// io.mkdir(path) - Create directory (recursive)
Item pn_io_mkdir(Item path_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.mkdir()"); return ItemNull; }
    StrBuf* path_buf = get_local_path_from_item(path_item);

    if (!path_buf) {
        log_error("io.mkdir: invalid path argument");
        return ItemError;
    }

    const char* path = path_buf->str;

    log_debug("fs.mkdir: %s", path);

    // Use platform command for recursive mkdir
#ifdef _WIN32
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "mkdir \"%s\" 2>nul", path);
    int result = system(cmd);
    // Windows mkdir returns error if dir exists, ignore it
    if (result != 0) {
        DWORD attr = GetFileAttributesA(path);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            result = 0;  // Directory exists, not an error
        }
    }
#else
    // Create directory recursively
    char* path_copy = strdup(path);
    char* p = path_copy;
    int result = 0;

    // Skip leading slash
    if (*p == '/') p++;

    while (*p) {
        while (*p && *p != '/') p++;
        char saved = *p;
        *p = '\0';

        if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
            result = -1;
            break;
        }

        *p = saved;
        if (saved) p++;
    }
    free(path_copy);
#endif

    if (result != 0) {
        log_error("io.mkdir: failed to create directory %s: %s", path, strerror(errno));
    }

    strbuf_free(path_buf);

    return (result != 0) ? ItemError : ItemNull;
}

// io.touch(path) - Create file or update modification time
Item pn_io_touch(Item path_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.touch()"); return ItemNull; }
    StrBuf* path_buf = get_local_path_from_item(path_item);

    if (!path_buf) {
        log_error("io.touch: invalid path argument");
        return ItemError;
    }

    const char* path = path_buf->str;

    log_debug("fs.touch: %s", path);

    // Check if file exists
    struct stat st;
    if (stat(path, &st) == 0) {
        // File exists - update modification time
#ifdef _WIN32
        HANDLE hFile = CreateFileA(path, FILE_WRITE_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            SetFileTime(hFile, NULL, NULL, &ft);
            CloseHandle(hFile);
        }
#else
        utime(path, NULL);  // NULL = current time
#endif
    } else {
        // File doesn't exist - create empty file
        FILE* f = fopen(path, "w");
        if (f) {
            fclose(f);
        } else {
            log_error("io.touch: failed to create file %s: %s", path, strerror(errno));
            strbuf_free(path_buf);
            return ItemError;
        }
    }

    strbuf_free(path_buf);
    return ItemNull;
}

// io.symlink(target, link) - Create symbolic link
Item pn_io_symlink(Item target_item, Item link_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.symlink()"); return ItemNull; }
    StrBuf* target_buf = get_local_path_from_item(target_item);
    StrBuf* link_buf = get_local_path_from_item(link_item);

    if (!target_buf || !link_buf) {
        log_error("io.symlink: invalid path argument");
        if (target_buf) strbuf_free(target_buf);
        if (link_buf) strbuf_free(link_buf);
        return ItemError;
    }

    const char* target_path = target_buf->str;
    const char* link_path = link_buf->str;

    log_debug("fs.symlink: %s -> %s", link_path, target_path);

#ifdef _WIN32
    // Windows requires admin privileges for symlinks
    // Use mklink command
    char cmd[4096];
    struct stat st;
    const char* type_flag = "";
    if (stat(target_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        type_flag = "/D ";
    }
    snprintf(cmd, sizeof(cmd), "mklink %s\"%s\" \"%s\"", type_flag, link_path, target_path);
    int result = system(cmd);
#else
    int result = symlink(target_path, link_path);
#endif

    if (result != 0) {
        log_error("io.symlink: failed to create symlink %s -> %s: %s",
                  link_path, target_path, strerror(errno));
    }

    strbuf_free(target_buf);
    strbuf_free(link_buf);

    return (result != 0) ? ItemError : ItemNull;
}

// io.chmod(path, mode) - Change file permissions
// mode can be string like "755" or int like 0755
Item pn_io_chmod(Item path_item, Item mode_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.chmod()"); return ItemNull; }
    StrBuf* path_buf = get_local_path_from_item(path_item);

    if (!path_buf) {
        log_error("io.chmod: invalid path argument");
        return ItemError;
    }

    const char* path = path_buf->str;

    // Parse mode
    int mode = 0;
    TypeId mode_type = get_type_id(mode_item);
    if (mode_type == LMD_TYPE_INT || mode_type == LMD_TYPE_INT64) {
        mode = (int)it2l(mode_item);
    } else if (mode_type == LMD_TYPE_STRING) {
        String* mode_str = mode_item.get_string();
        if (mode_str && mode_str->chars) {
            // Parse octal string like "755"
            mode = (int)strtol(mode_str->chars, NULL, 8);
        }
    } else {
        log_error("io.chmod: mode must be int or string");
        strbuf_free(path_buf);
        return ItemError;
    }

    log_debug("io.chmod: %s mode=%o", path, mode);

#ifdef _WIN32
    // Windows doesn't have chmod in the same way
    // Can only toggle read-only flag
    int result = _chmod(path, mode);
#else
    int result = chmod(path, mode);
#endif

    if (result != 0) {
        log_error("io.chmod: failed to change mode of %s: %s", path, strerror(errno));
    }

    strbuf_free(path_buf);

    return (result != 0) ? ItemError : ItemNull;
}

// io.rename(old_path, new_path) - Rename file or directory (same as move but clearer intent)
Item pn_io_rename(Item old_item, Item new_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.rename()"); return ItemNull; }
    StrBuf* old_buf = get_local_path_from_item(old_item);
    StrBuf* new_buf = get_local_path_from_item(new_item);

    if (!old_buf || !new_buf) {
        log_error("io.rename: invalid path argument");
        if (old_buf) strbuf_free(old_buf);
        if (new_buf) strbuf_free(new_buf);
        return ItemError;
    }

    const char* old_path = old_buf->str;
    const char* new_path = new_buf->str;

    log_debug("io.rename: %s -> %s", old_path, new_path);

    int result = rename(old_path, new_path);

    if (result != 0) {
        log_error("io.rename: failed to rename %s to %s: %s", old_path, new_path, strerror(errno));
    }

    strbuf_free(old_buf);
    strbuf_free(new_buf);

    return (result != 0) ? ItemError : ItemNull;
}

// io.fetch(target) - Fetch content from URL or local file (1-arg version)
Item pn_io_fetch1(Item target_item) {
    return pn_fetch(target_item, ItemNull);
}

// io.fetch(target, options) - Fetch content from URL or local file (2-arg version)
Item pn_io_fetch2(Item target_item, Item options_item) {
    return pn_fetch(target_item, options_item);
}
