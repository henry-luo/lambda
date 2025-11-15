/**
 * @file error_reporting.c
 * @brief Validation Error Reporting and Formatting
 * @author Henry Luo
 * @license MIT
 */

#include "validator.hpp"
#include "../lambda-data.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/arraylist.h"
#include <string.h>
#include <assert.h>

// Forward declarations for functions used in this file
List* suggest_corrections(ValidationError* error, Pool* pool);
String* format_validation_path(PathSegment* path, Pool* pool);
String* format_type_name(void* type, Pool* pool);
StrView strview_from_cstr(const char* str);

// helper to get type name from type_info
static const char* get_type_name_str(TypeId type_id) {
    // type_info array is sized 32 in lambda-data.cpp
    if (type_id >= 0 && type_id < 32) {
        return type_info[type_id].name;
    }
    return "unknown";
}

static void stringbuf_append_string(StringBuf* sb, String* str) {
    if (str && str->chars) {
        stringbuf_append_str_n(sb, str->chars, str->len);
    }
}

// Note: Utility functions moved to avoid duplicates
List* suggest_similar_names(const char* name, List* available_names, Pool* pool) {
    // Return NULL for now - suggestions not implemented
    (void)name;
    (void)available_names;
    (void)pool;
    return nullptr;
}

List* suggest_corrections(ValidationError* error, Pool* pool) {
    // Return NULL for now - suggestions not implemented
    (void)error;
    (void)pool;
    return nullptr;
}

// ==================== Error Message Formatting ====================

const char* get_error_code_name(ValidationErrorCode code) {
    switch (code) {
        case VALID_ERROR_NONE:
            return "NO_ERROR";
        case VALID_ERROR_TYPE_MISMATCH:
            return "TYPE_MISMATCH";
        case VALID_ERROR_MISSING_FIELD:
            return "MISSING_FIELD";
        case VALID_ERROR_UNEXPECTED_FIELD:
            return "UNEXPECTED_FIELD";
        case VALID_ERROR_NULL_VALUE:
            return "NULL_VALUE";
        case VALID_ERROR_INVALID_ELEMENT:
            return "INVALID_ELEMENT";
        case VALID_ERROR_CONSTRAINT_VIOLATION:
            return "CONSTRAINT_VIOLATION";
        case VALID_ERROR_REFERENCE_ERROR:
            return "REFERENCE_ERROR";
        case VALID_ERROR_OCCURRENCE_ERROR:
            return "OCCURRENCE_ERROR";
        case VALID_ERROR_CIRCULAR_REFERENCE:
            return "CIRCULAR_REFERENCE";
        case VALID_ERROR_PARSE_ERROR:
            return "PARSE_ERROR";
        default:
            return "UNKNOWN_ERROR";
    }
}

String* format_error_with_context(ValidationError* error, Pool* pool) {
    if (!error || !pool) return nullptr;

    StringBuf* sb = stringbuf_new(pool);

    // Error code and message
    const char* error_code = get_error_code_name(error->code);
    stringbuf_append_format(sb, "[%s] ", error_code);

    const char* error_msg = error->message ? error->message->chars : "Unknown error";
    stringbuf_append_str(sb, error_msg);

    // Path information
    if (error->path) {
        stringbuf_append_str(sb, " at ");
        String* path_str = format_validation_path(error->path, pool);
        if (path_str && path_str->chars) {
            stringbuf_append_str(sb, path_str->chars);
        }
    }

    // Expected vs Actual type information
    if (error->expected || error->actual.item) {
        stringbuf_append_str(sb, "\n    ");

        if (error->expected) {
            Type* expected = (Type*)error->expected;
            stringbuf_append_format(sb, "Expected: %s", get_type_name_str(expected->type_id));
        }

        if (error->actual.item) {
            if (error->expected) {
                stringbuf_append_str(sb, ", ");
            }
            stringbuf_append_format(sb, "Actual: %s", get_type_name_str(error->actual.type_id()));
        }
    }

    // Suggestions
    if (error->suggestions && error->suggestions->length > 0) {
        stringbuf_append_str(sb, "\n    Suggestions:");
        for (int i = 0; i < error->suggestions->length; i++) {
            ConstItem suggestion_item = error->suggestions->get(i);
            if (suggestion_item.type_id() == LMD_TYPE_STRING) {
                String* suggestion = suggestion_item.string();
                if (suggestion && suggestion->chars) {
                    stringbuf_append_format(sb, "\n      - %s", suggestion->chars);
                }
            }
        }
    }

    return stringbuf_to_string(sb);
}

// ==================== Validation Report Generation ====================

String* generate_validation_report(ValidationResult* result, Pool* pool) {
    if (!result) {
        return string_from_strview(strview_from_cstr("No validation result"), pool);
    }

    StringBuf* report = stringbuf_new(pool);

    // Header
    if (result->valid) {
        stringbuf_append_str(report, "✓ Validation successful\n");
    } else {
        stringbuf_append_str(report, "✗ Validation failed\n");
    }

    // Summary
    char summary[256];
    snprintf(summary, sizeof(summary),
             "Errors: %d, Warnings: %d\n",
             result->error_count, result->warning_count);
    stringbuf_append_str(report, summary);

    if (result->error_count > 0 || result->warning_count > 0) {
        stringbuf_append_str(report, "\n");
    }

    // Errors
    if (result->error_count > 0) {
        stringbuf_append_str(report, "Errors:\n");
        ValidationError* error = result->errors;
        int error_num = 1;

        while (error) {
            char error_prefix[32];
            snprintf(error_prefix, sizeof(error_prefix), "  %d. ", error_num++);
            stringbuf_append_str(report, error_prefix);

            String* error_str = format_error_with_context(error, pool);
            stringbuf_append_string(report, error_str);
            stringbuf_append_str(report, "\n");

            error = error->next;
        }
        stringbuf_append_str(report, "\n");
    }

    // Warnings
    if (result->warning_count > 0) {
        stringbuf_append_str(report, "Warnings:\n");
        ValidationWarning* warning = result->warnings;
        int warning_num = 1;

        while (warning) {
            char warning_prefix[32];
            snprintf(warning_prefix, sizeof(warning_prefix), "  %d. ", warning_num++);
            stringbuf_append_str(report, warning_prefix);

            String* warning_str = format_error_with_context((ValidationError*)warning, pool);
            stringbuf_append_string(report, warning_str);
            stringbuf_append_str(report, "\n");

            warning = warning->next;
        }
    }

    return stringbuf_to_string(report);
}

// ==================== JSON Report Generation ====================

String* generate_json_report(ValidationResult* result, Pool* pool) {
    if (!result) {
        return string_from_strview(strview_from_cstr("{\"error\": \"No validation result\"}"), pool);
    }

    StringBuf* json = stringbuf_new(pool);

    stringbuf_append_str(json, "{\n");
    stringbuf_append_str(json, "  \"valid\": ");
    stringbuf_append_str(json, result->valid ? "true" : "false");
    stringbuf_append_str(json, ",\n");

    char counts[128];
    snprintf(counts, sizeof(counts),
             "  \"error_count\": %d,\n  \"warning_count\": %d",
             result->error_count, result->warning_count);
    stringbuf_append_str(json, counts);
    // Errors array
    if (result->error_count > 0) {
        stringbuf_append_str(json, ",\n  \"errors\": [\n");
        ValidationError* error = result->errors;
        bool first = true;

        while (error) {
            if (!first) {
                stringbuf_append_str(json, ",\n");
            }
            first = false;

            stringbuf_append_str(json, "    {\n");

            // Error code
            char code_str[64];
            snprintf(code_str, sizeof(code_str),
                     "      \"code\": \"%s\"", get_error_code_name(error->code));
            stringbuf_append_str(json, code_str);

            // Message
            if (error->message) {
                stringbuf_append_str(json, ",\n      \"message\": \"");
                // Escape JSON string (simplified)
                for (size_t i = 0; i < error->message->len; i++) {
                    char c = error->message->chars[i];
                    if (c == '"' || c == '\\') {
                        stringbuf_append_char(json, '\\');
                    }
                    stringbuf_append_char(json, c);
                }
                stringbuf_append_str(json, "\"");
            }

            stringbuf_append_str(json, "\n    }");
            error = error->next;
        }

        stringbuf_append_str(json, "\n  ]");
    }

    // Warnings array (similar structure)
    if (result->warning_count > 0) {
        stringbuf_append_str(json, ",\n  \"warnings\": [\n");
        ValidationWarning* warning = result->warnings;
        bool first = true;

        while (warning) {
            if (!first) {
                stringbuf_append_str(json, ",\n");
            }
            first = false;

            stringbuf_append_str(json, "    {\n");

            char code_str[64];
            snprintf(code_str, sizeof(code_str),
                     "      \"code\": \"%s\"", get_error_code_name(warning->code));
            stringbuf_append_str(json, code_str);

            if (warning->message) {
                stringbuf_append_str(json, ",\n      \"message\": \"");
                stringbuf_append_string(json, warning->message);
                stringbuf_append_str(json, "\"");
            }

            stringbuf_append_str(json, "\n    }");
            warning = warning->next;
        }

        stringbuf_append_str(json, "\n  ]");
    }

    stringbuf_append_str(json, "\n}");

    return stringbuf_to_string(json);
}

// Format validation path
String* format_validation_path(PathSegment* path, Pool* pool) {
    if (!path) {
        return string_from_strview(strview_from_cstr("(root)"), pool);
    }

    StringBuf* sb = stringbuf_new(pool);

    // Collect path segments in reverse order (path is stored backwards)
    PathSegment* segments[100];
    int count = 0;
    PathSegment* current = path;
    while (current && count < 100) {
        segments[count++] = current;
        current = current->next;
    }

    // Build path string from root to leaf
    for (int i = count - 1; i >= 0; i--) {
        PathSegment* seg = segments[i];
        switch (seg->type) {
            case PATH_FIELD:
                stringbuf_append_format(sb, ".%.*s",
                    (int)seg->data.field_name.length, seg->data.field_name.str);
                break;
            case PATH_INDEX:
                stringbuf_append_format(sb, "[%ld]", seg->data.index);
                break;
            case PATH_ELEMENT:
                stringbuf_append_format(sb, "<%.*s>",
                    (int)seg->data.element_tag.length, seg->data.element_tag.str);
                break;
            case PATH_ATTRIBUTE:
                stringbuf_append_format(sb, "@%.*s",
                    (int)seg->data.attr_name.length, seg->data.attr_name.str);
                break;
            case PATH_UNION:
                stringbuf_append_format(sb, "|%ld", seg->data.index);
                break;
        }
    }

    return stringbuf_to_string(sb);
}

// Format type name
String* format_type_name(void* type, Pool* pool) {
    // Simple type name formatting - just return "unknown" for now
    return string_from_strview(strview_from_cstr("unknown"), pool);
}

void print_validation_result(ValidationResult* result) {
    if (!result) {
        log_debug("NULL validation result");
        return;
    }

    log_debug("Validation Result:");
    log_debug("  Valid: %s", result->valid ? "true" : "false");
    log_debug("  Errors: %d", result->error_count);
    log_debug("  Warnings: %d", result->warning_count);

    if (result->error_count > 0) {
        log_debug("  Error details:");
        ValidationError* error = result->errors;
        int i = 1;
        while (error) {
            log_debug("    %d. [%s] %s", i++,
                   get_error_code_name(error->code),
                   error->message ? error->message->chars : "No message");
            error = error->next;
        }
    }
}

void print_validation_path(PathSegment* path, StringBuf* sb) {
    if (!path) {
        stringbuf_append_str(sb, "(root)");
        return;
    }

    // Print path in reverse order
    PathSegment* segments[100];
    int count = 0;

    PathSegment* current = path;
    while (current && count < 100) {
        segments[count++] = current;
        current = current->next;
    }

    for (int i = count - 1; i >= 0; i--) {
        PathSegment* segment = segments[i];
        switch (segment->type) {
            case PATH_FIELD:
                stringbuf_append_format(sb, ".%.*s", (int)segment->data.field_name.length,
                       segment->data.field_name.str);
                break;
            case PATH_INDEX:
                stringbuf_append_format(sb, "[%ld]", segment->data.index);
                break;
            case PATH_ELEMENT:
                stringbuf_append_format(sb, "<%.*s>", (int)segment->data.element_tag.length,
                       segment->data.element_tag.str);
                break;
            case PATH_ATTRIBUTE:
                stringbuf_append_format(sb, "@%.*s", (int)segment->data.attr_name.length,
                       segment->data.attr_name.str);
                break;
        }
    }
}
