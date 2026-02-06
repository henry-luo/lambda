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

Item pn_print(Item item) {
    TypeId type_id = get_type_id(item);
    log_debug("pn_print: %d", type_id);
    String *str = fn_string(item);
    if (str) {
        printf("%s", str->chars);
    }
    return ItemNull;
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
        log_error("pn_output_internal: target must be string, symbol, or path, got type %d", target_type);
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
        String* str = value.get_string();
        if (str && str->chars) {
            return str->chars;
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

// 3-parameter wrapper: output(source, target, options) - with options map
// Options map supports:
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
    } else if (options_type != LMD_TYPE_NULL) {
        log_error("pn_output3: options must be a map or null, got type %d", options_type);
        return ItemError;
    }
    
    return pn_output_internal(source, target_item, format_str, append, atomic);
}

// 4-parameter wrapper: output(source, url, format, mode) - for pipe operators backward compat
// mode is 'write' or 'append'
// Returns: bytes written on success, error on failure
Item pn_output4(Item source, Item target_item, Item format_item, Item mode_item) {
    // determine append mode based on mode parameter
    bool append = false;
    if (get_type_id(mode_item) != LMD_TYPE_NULL) {
        String* mode_str = it2s(mode_item);
        if (mode_str && mode_str->chars && strcmp(mode_str->chars, "append") == 0) {
            append = true;
        }
    }
    
    // get format string
    const char* format_str = NULL;
    TypeId format_type = get_type_id(format_item);
    
    if (format_type == LMD_TYPE_SYMBOL) {
        String* format_sym = it2s(format_item);
        if (format_sym && format_sym->chars) {
            format_str = format_sym->chars;
        }
    } else if (format_type == LMD_TYPE_STRING) {
        String* format_s = it2s(format_item);
        if (format_s && format_s->chars) {
            format_str = format_s->chars;
        }
    }
    // NULL format means auto-detect
    
    return pn_output_internal(source, target_item, format_str, append, false);
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
    log_debug("pn_fetch called");
    // Validate URL parameter
    String* url_str;
    if (url._type_id != LMD_TYPE_STRING && url._type_id != LMD_TYPE_SYMBOL) {
        log_debug("fetch url must be a string or symbol, got type %d", url._type_id);
        return ItemError;
    }
    url_str = url.get_string();

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
            String* method_str = method_item.get_string();
            config.method = method_str->chars;
        }

        // Extract body
        Item body_key = create_string_item("body");
        Item body_item = map_get(options_map, body_key);
        if (body_item.item) {
            if (body_item._type_id == LMD_TYPE_STRING || body_item._type_id == LMD_TYPE_SYMBOL) {
                String* body_str = body_item.get_string();
                config.body = body_str->chars;
                config.body_size = body_str->len;
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
        log_debug("fetch options must be a map or null, got type %d", options_type);
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

Item pn_cmd(Item cmd, Item args) {
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
    String* result_str;  Item result;
    if (output_buf->length > 0) {
        result_str = heap_strcpy(output_buf->str, output_buf->length);
        result = {.item = s2it(result_str)};
    } else {
        // Return empty string if no output
        result = ItemNull;
    }
    log_debug("pn_cmd: command output length: %s", result_str->chars);

    strbuf_free(output_buf);
    // For now, return the stdout output as a string
    // TODO: Could return a map with {stdout: string, exit_code: int} for more info
    return result;
}

// ============================================================================
// File System Functions (fs module)
// These are procedural functions for file/directory operations
// ============================================================================

// Helper: Get path string from Item (supports Path, String, and Symbol types)
static const char* get_path_string(Item path_item, StrBuf** buf_out) {
    TypeId type_id = get_type_id(path_item);
    if (type_id == LMD_TYPE_PATH) {
        Path* path = path_item.path;
        if (!path) return NULL;
        StrBuf* buf = strbuf_new();
        path_to_os_path(path, buf);
        *buf_out = buf;
        return buf->str;
    } else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        String* str = path_item.get_string();
        if (!str) return NULL;
        *buf_out = NULL;
        return str->chars;
    }
    return NULL;
}

// Helper: Free path buffer if allocated
static void free_path_buf(StrBuf* buf) {
    if (buf) strbuf_free(buf);
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
        log_error("fs.delete: failed to remove %s: %s", path, strerror(errno));
    }
    return rv;
}
#endif

// fs.copy(src, dst) - Copy file or directory
Item pn_fs_copy(Item src_item, Item dst_item) {
    StrBuf *src_buf = NULL, *dst_buf = NULL;
    const char* src_path = get_path_string(src_item, &src_buf);
    const char* dst_path = get_path_string(dst_item, &dst_buf);
    
    if (!src_path || !dst_path) {
        log_error("fs.copy: invalid path argument");
        free_path_buf(src_buf);
        free_path_buf(dst_buf);
        return ItemError;
    }
    
    log_debug("fs.copy: %s -> %s", src_path, dst_path);
    
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
    free_path_buf(src_buf);
    free_path_buf(dst_buf);
    
    if (result != 0) {
        log_error("fs.copy: failed to copy %s to %s", src_path, dst_path);
        return ItemError;
    }
    return ItemNull;
}

// fs.move(src, dst) - Move/rename file or directory
Item pn_fs_move(Item src_item, Item dst_item) {
    StrBuf *src_buf = NULL, *dst_buf = NULL;
    const char* src_path = get_path_string(src_item, &src_buf);
    const char* dst_path = get_path_string(dst_item, &dst_buf);
    
    if (!src_path || !dst_path) {
        log_error("fs.move: invalid path argument");
        free_path_buf(src_buf);
        free_path_buf(dst_buf);
        return ItemError;
    }
    
    log_debug("fs.move: %s -> %s", src_path, dst_path);
    
    int result = rename(src_path, dst_path);
    
    // If rename fails (cross-device), fall back to copy+delete
    if (result != 0 && errno == EXDEV) {
        log_debug("fs.move: cross-device move, using copy+delete");
        Item copy_result = pn_fs_copy(src_item, dst_item);
        if (copy_result.item == ItemError.item) {
            free_path_buf(src_buf);
            free_path_buf(dst_buf);
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
    
    free_path_buf(src_buf);
    free_path_buf(dst_buf);
    
    if (result != 0) {
        log_error("fs.move: failed to move %s to %s: %s", src_path, dst_path, strerror(errno));
        return ItemError;
    }
    return ItemNull;
}

// fs.delete(path) - Delete file or directory
Item pn_fs_delete(Item path_item) {
    StrBuf* path_buf = NULL;
    const char* path = get_path_string(path_item, &path_buf);
    
    if (!path) {
        log_error("fs.delete: invalid path argument");
        return ItemError;
    }
    
    log_debug("fs.delete: %s", path);
    
    struct stat st;
    if (stat(path, &st) != 0) {
        log_error("fs.delete: path does not exist: %s", path);
        free_path_buf(path_buf);
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
    
    free_path_buf(path_buf);
    
    if (result != 0) {
        log_error("fs.delete: failed to delete %s: %s", path, strerror(errno));
        return ItemError;
    }
    return ItemNull;
}

// fs.mkdir(path) - Create directory (recursive)
Item pn_fs_mkdir(Item path_item) {
    StrBuf* path_buf = NULL;
    const char* path = get_path_string(path_item, &path_buf);
    
    if (!path) {
        log_error("fs.mkdir: invalid path argument");
        return ItemError;
    }
    
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
    
    free_path_buf(path_buf);
    
    if (result != 0) {
        log_error("fs.mkdir: failed to create directory %s: %s", path, strerror(errno));
        return ItemError;
    }
    return ItemNull;
}

// fs.touch(path) - Create file or update modification time
Item pn_fs_touch(Item path_item) {
    StrBuf* path_buf = NULL;
    const char* path = get_path_string(path_item, &path_buf);
    
    if (!path) {
        log_error("fs.touch: invalid path argument");
        return ItemError;
    }
    
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
            log_error("fs.touch: failed to create file %s: %s", path, strerror(errno));
            free_path_buf(path_buf);
            return ItemError;
        }
    }
    
    free_path_buf(path_buf);
    return ItemNull;
}

// fs.symlink(target, link) - Create symbolic link
Item pn_fs_symlink(Item target_item, Item link_item) {
    StrBuf *target_buf = NULL, *link_buf = NULL;
    const char* target_path = get_path_string(target_item, &target_buf);
    const char* link_path = get_path_string(link_item, &link_buf);
    
    if (!target_path || !link_path) {
        log_error("fs.symlink: invalid path argument");
        free_path_buf(target_buf);
        free_path_buf(link_buf);
        return ItemError;
    }
    
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
    
    free_path_buf(target_buf);
    free_path_buf(link_buf);
    
    if (result != 0) {
        log_error("fs.symlink: failed to create symlink %s -> %s: %s", 
                  link_path, target_path, strerror(errno));
        return ItemError;
    }
    return ItemNull;
}

// fs.chmod(path, mode) - Change file permissions
// mode can be string like "755" or int like 0755
Item pn_fs_chmod(Item path_item, Item mode_item) {
    StrBuf* path_buf = NULL;
    const char* path = get_path_string(path_item, &path_buf);
    
    if (!path) {
        log_error("fs.chmod: invalid path argument");
        return ItemError;
    }
    
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
        log_error("fs.chmod: mode must be int or string");
        free_path_buf(path_buf);
        return ItemError;
    }
    
    log_debug("fs.chmod: %s mode=%o", path, mode);
    
#ifdef _WIN32
    // Windows doesn't have chmod in the same way
    // Can only toggle read-only flag
    int result = _chmod(path, mode);
#else
    int result = chmod(path, mode);
#endif
    
    free_path_buf(path_buf);
    
    if (result != 0) {
        log_error("fs.chmod: failed to change mode of %s: %s", path, strerror(errno));
        return ItemError;
    }
    return ItemNull;
}

// fs.rename(old_path, new_path) - Rename file or directory (same as move but clearer intent)
Item pn_fs_rename(Item old_item, Item new_item) {
    StrBuf *old_buf = NULL, *new_buf = NULL;
    const char* old_path = get_path_string(old_item, &old_buf);
    const char* new_path = get_path_string(new_item, &new_buf);
    
    if (!old_path || !new_path) {
        log_error("fs.rename: invalid path argument");
        free_path_buf(old_buf);
        free_path_buf(new_buf);
        return ItemError;
    }
    
    log_debug("fs.rename: %s -> %s", old_path, new_path);
    
    int result = rename(old_path, new_path);
    
    free_path_buf(old_buf);
    free_path_buf(new_buf);
    
    if (result != 0) {
        log_error("fs.rename: failed to rename %s to %s: %s", old_path, new_path, strerror(errno));
        return ItemError;
    }
    return ItemNull;
}

