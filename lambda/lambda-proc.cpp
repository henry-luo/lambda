#include "transpiler.hpp"
#include "../lib/log.h"
#include "utf_string.h"

#include <stdarg.h>
#include <time.h>
#include <errno.h>  // for errno checking
#ifdef _WIN32
#include <windows.h>
#include <process.h>
// Windows doesn't have these macros, provide simple equivalents
#define WIFEXITED(status) (1)
#define WEXITSTATUS(status) (status)
#else
#include <sys/wait.h>  // for WIFEXITED, WEXITSTATUS
#endif
#include <string.h>  // for strlen
#include "input/input.h"

extern __thread Context* context;

Item pn_print(Item item) {
    TypeId type_id = get_type_id(item);
    log_debug("pn_print: %d", type_id);
    String *str = fn_string(item);
    if (str) {
        printf("%s", str->chars);
    }
    return ItemNull;
}

// Helper function to free FetchResponse from within Lambda context
void free_fetch_response(FetchResponse* response) {
    if (!response) return;

    if (response->data) {
        free(response->data);
        response->data = NULL;
    }

    if (response->content_type) {
        free(response->content_type);
        response->content_type = NULL;
    }

    for (int i = 0; i < response->response_header_count; i++) {
        free(response->response_headers[i]);
    }
    if (response->response_headers) {
        free(response->response_headers);
        response->response_headers = NULL;
    }
    response->response_header_count = 0;

    free(response);
}

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
    url_str = (String*)url.pointer;

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
        return (Item){.pointer = (uint64_t)(uintptr_t)string, ._type_id = LMD_TYPE_STRING};
    };

    // Parse options if provided
    TypeId options_type = get_type_id(options);
    if (options_type == LMD_TYPE_MAP) {
        Map* options_map = options.map;

        // Extract method
        Item method_key = create_string_item("method");
        Item method_item = map_get(options_map, method_key);
        if (method_item.item && (method_item._type_id == LMD_TYPE_STRING || method_item._type_id == LMD_TYPE_SYMBOL)) {
            String* method_str = (String*)method_item.pointer;
            config.method = method_str->chars;
        }

        // Extract body
        Item body_key = create_string_item("body");
        Item body_item = map_get(options_map, body_key);
        if (body_item.item) {
            if (body_item._type_id == LMD_TYPE_STRING || body_item._type_id == LMD_TYPE_SYMBOL) {
                String* body_str = (String*)body_item.pointer;
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
                value_item._type_id = LMD_TYPE_INT;
                value_item.int_val = *(int*)field_ptr;
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

    String* cmd_str = (String*)cmd.pointer;
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
