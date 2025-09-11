/**
 * @file error_reporting.c
 * @brief Validation Error Reporting and Formatting
 * @author Henry Luo
 * @license MIT
 */

#include "../validator.hpp"
#include "../lambda-data.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/arraylist.h"
#include <string.h>
#include <assert.h>

// Forward declarations for functions used in this file
extern "C" List* suggest_corrections(ValidationError* error, VariableMemPool* pool);
String* format_validation_path(PathSegment* path, VariableMemPool* pool);
String* format_type_name(void* type, VariableMemPool* pool);
StrView strview_from_cstr(const char* str);

// Helper function implementations
static void stringbuf_append_cstr(StringBuf* sb, const char* str) {
    stringbuf_append_str(sb, str);
}

static void stringbuf_append_string(StringBuf* sb, String* str) {
    if (str && str->chars) {
        stringbuf_append_str_n(sb, str->chars, str->len);
    }
}

// Note: Utility functions moved to avoid duplicates



List* suggest_similar_names(const char* name, List* available_names, VariableMemPool* pool) {
    // Return NULL for now - suggestions not implemented
    (void)name;
    (void)available_names;
    (void)pool;
    return nullptr;
}

List* suggest_corrections(ValidationError* error, VariableMemPool* pool) {
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

String* format_error_with_context(ValidationError* error, VariableMemPool* pool) {
    if (!error || !pool) return nullptr;
    
    const char* error_msg = error->message ? error->message->chars : "Unknown error";
    size_t len = strlen(error_msg);
    String* formatted = (String*)pool_calloc(pool, sizeof(String) + len + 1);
    if (formatted) {
        formatted->len = len;
        formatted->ref_cnt = 0;
        strcpy(formatted->chars, error_msg);
    }
    
    return formatted;
}

// ==================== Validation Report Generation ====================

String* generate_validation_report(ValidationResult* result, VariableMemPool* pool) {
    if (!result) {
        return string_from_strview(strview_from_cstr("No validation result"), pool);
    }
    
    StringBuf* report = stringbuf_new(pool);
    
    // Header
    if (result->valid) {
        stringbuf_append_cstr(report, "✓ Validation successful\n");
    } else {
        stringbuf_append_cstr(report, "✗ Validation failed\n");
    }
    
    // Summary
    char summary[256];
    snprintf(summary, sizeof(summary), 
             "Errors: %d, Warnings: %d\n", 
             result->error_count, result->warning_count);
    stringbuf_append_cstr(report, summary);
    
    if (result->error_count > 0 || result->warning_count > 0) {
        stringbuf_append_cstr(report, "\n");
    }
    
    // Errors
    if (result->error_count > 0) {
        stringbuf_append_cstr(report, "Errors:\n");
        ValidationError* error = result->errors;
        int error_num = 1;
        
        while (error) {
            char error_prefix[32];
            snprintf(error_prefix, sizeof(error_prefix), "  %d. ", error_num++);
            stringbuf_append_cstr(report, error_prefix);
            
            String* error_str = format_error_with_context(error, pool);
            stringbuf_append_string(report, error_str);
            stringbuf_append_cstr(report, "\n");
            
            error = error->next;
        }
        stringbuf_append_cstr(report, "\n");
    }
    
    // Warnings
    if (result->warning_count > 0) {
        stringbuf_append_cstr(report, "Warnings:\n");
        ValidationWarning* warning = result->warnings;
        int warning_num = 1;
        
        while (warning) {
            char warning_prefix[32];
            snprintf(warning_prefix, sizeof(warning_prefix), "  %d. ", warning_num++);
            stringbuf_append_cstr(report, warning_prefix);
            
            String* warning_str = format_error_with_context((ValidationError*)warning, pool);
            stringbuf_append_string(report, warning_str);
            stringbuf_append_cstr(report, "\n");
            
            warning = warning->next;
        }
    }
    
    return stringbuf_to_string(report);
}

// ==================== JSON Report Generation ====================

String* generate_json_report(ValidationResult* result, VariableMemPool* pool) {
    if (!result) {
        return string_from_strview(strview_from_cstr("{\"error\": \"No validation result\"}"), pool);
    }
    
    StringBuf* json = stringbuf_new(pool);
    
    stringbuf_append_cstr(json, "{\n");
    stringbuf_append_cstr(json, "  \"valid\": ");
    stringbuf_append_cstr(json, result->valid ? "true" : "false");
    stringbuf_append_cstr(json, ",\n");
    
    char counts[128];
    snprintf(counts, sizeof(counts), 
             "  \"error_count\": %d,\n  \"warning_count\": %d", 
             result->error_count, result->warning_count);
    stringbuf_append_cstr(json, counts);
    // Errors array
    if (result->error_count > 0) {
        stringbuf_append_cstr(json, ",\n  \"errors\": [\n");
        ValidationError* error = result->errors;
        bool first = true;
        
        while (error) {
            if (!first) {
                stringbuf_append_cstr(json, ",\n");
            }
            first = false;
            
            stringbuf_append_cstr(json, "    {\n");
            
            // Error code
            char code_str[64];
            snprintf(code_str, sizeof(code_str), 
                     "      \"code\": \"%s\"", get_error_code_name(error->code));
            stringbuf_append_cstr(json, code_str);
            
            // Message
            if (error->message) {
                stringbuf_append_cstr(json, ",\n      \"message\": \"");
                // Escape JSON string (simplified)
                for (size_t i = 0; i < error->message->len; i++) {
                    char c = error->message->chars[i];
                    if (c == '"' || c == '\\') {
                        stringbuf_append_char(json, '\\');
                    }
                    stringbuf_append_char(json, c);
                }
                stringbuf_append_cstr(json, "\"");
            }
            
            stringbuf_append_cstr(json, "\n    }");
            error = error->next;
        }
        
        stringbuf_append_cstr(json, "\n  ]");
    }
    
    // Warnings array (similar structure)
    if (result->warning_count > 0) {
        stringbuf_append_cstr(json, ",\n  \"warnings\": [\n");
        ValidationWarning* warning = result->warnings;
        bool first = true;
        
        while (warning) {
            if (!first) {
                stringbuf_append_cstr(json, ",\n");
            }
            first = false;
            
            stringbuf_append_cstr(json, "    {\n");
            
            char code_str[64];
            snprintf(code_str, sizeof(code_str), 
                     "      \"code\": \"%s\"", get_error_code_name(warning->code));
            stringbuf_append_cstr(json, code_str);
            
            if (warning->message) {
                stringbuf_append_cstr(json, ",\n      \"message\": \"");
                stringbuf_append_string(json, warning->message);
                stringbuf_append_cstr(json, "\"");
            }
            
            stringbuf_append_cstr(json, "\n    }");
            warning = warning->next;
        }
        
        stringbuf_append_cstr(json, "\n  ]");
    }
    
    stringbuf_append_cstr(json, "\n}");
    
    return stringbuf_to_string(json);
}

// ==================== Debug Utilities ====================

// ==================== Missing Function Implementations ====================

// Format validation path
String* format_validation_path(PathSegment* path, VariableMemPool* pool) {
    if (!path) {
        return string_from_strview(strview_from_cstr(""), pool);
    }
    
    // Simple path formatting - just return empty string for now
    return string_from_strview(strview_from_cstr(""), pool);
}

// Format type name
String* format_type_name(void* type, VariableMemPool* pool) {
    // Simple type name formatting - just return "unknown" for now
    return string_from_strview(strview_from_cstr("unknown"), pool);
}

#ifdef DEBUG
void print_validation_result(ValidationResult* result) {
    if (!result) {
        printf("NULL validation result\n");
        return;
    }
    
    printf("Validation Result:\n");
    printf("  Valid: %s\n", result->valid ? "true" : "false");
    printf("  Errors: %d\n", result->error_count);
    printf("  Warnings: %d\n", result->warning_count);
    
    if (result->error_count > 0) {
        printf("  Error details:\n");
        ValidationError* error = result->errors;
        int i = 1;
        while (error) {
            printf("    %d. [%s] %s\n", i++, 
                   get_error_code_name(error->code),
                   error->message ? error->message->chars : "No message");
            error = error->next;
        }
    }
}

void print_validation_path(PathSegment* path) {
    if (!path) {
        printf("(root)");
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
                printf(".%.*s", (int)segment->data.field_name.length, 
                       segment->data.field_name.str);
                break;
            case PATH_INDEX:
                printf("[%ld]", segment->data.index);
                break;
            case PATH_ELEMENT:
                printf("<%.*s>", (int)segment->data.element_tag.length, 
                       segment->data.element_tag.str);
                break;
            case PATH_ATTRIBUTE:
                printf("@%.*s", (int)segment->data.attr_name.length, 
                       segment->data.attr_name.str);
                break;
        }
    }
}
#endif
