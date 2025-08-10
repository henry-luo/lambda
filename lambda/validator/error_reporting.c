/**
 * @file error_reporting.c
 * @brief Validation Error Reporting and Formatting
 * @author Henry Luo
 * @license MIT
 */

#include "validator.h"
#include "../../lib/strbuf.h"
#include <string.h>
#include <assert.h>

// Helper function implementations
static void strbuf_append_cstr(StrBuf* sb, const char* str) {
    strbuf_append_str(sb, str);
}

static void strbuf_append_string(StrBuf* sb, String* str) {
    if (str && str->chars) {
        strbuf_append_str_n(sb, str->chars, str->len);
    }
}

static String* strbuf_to_string(StrBuf* sb) {
    if (!sb || !sb->str) {
        return NULL;
    }
    
    // Create a String structure (assuming Lambda's String structure)
    String* result = (String*)malloc(sizeof(String) + sb->length + 1);
    if (!result) return NULL;
    
    result->len = sb->length;
    result->ref_cnt = 1;
    memcpy(result->chars, sb->str, sb->length);
    result->chars[sb->length] = '\0';
    
    return result;
}

// ==================== Validation Result Management ====================

ValidationResult* create_validation_result(VariableMemPool* pool) {
    ValidationResult* result = (ValidationResult*)pool_calloc(pool, sizeof(ValidationResult));
    result->valid = true;
    result->errors = NULL;
    result->warnings = NULL;
    result->error_count = 0;
    result->warning_count = 0;
    return result;
}

void validation_result_destroy(ValidationResult* result) {
    // Memory cleanup handled by pool
    (void)result;
}

void add_validation_error(ValidationResult* result, ValidationError* error) {
    if (!result || !error) return;
    
    // Add to linked list
    error->next = result->errors;
    result->errors = error;
    result->error_count++;
    result->valid = false;
}

void add_validation_warning(ValidationResult* result, ValidationWarning* warning) {
    if (!result || !warning) return;
    
    // Add to linked list
    warning->next = result->warnings;
    result->warnings = warning;
    result->warning_count++;
}

void merge_validation_results(ValidationResult* dest, ValidationResult* src) {
    if (!dest || !src) return;
    
    // Merge errors
    ValidationError* error = src->errors;
    while (error) {
        ValidationError* next = error->next;
        error->next = dest->errors;
        dest->errors = error;
        dest->error_count++;
        error = next;
    }
    
    // Merge warnings
    ValidationWarning* warning = src->warnings;
    while (warning) {
        ValidationWarning* next = warning->next;
        warning->next = dest->warnings;
        dest->warnings = warning;
        dest->warning_count++;
        warning = next;
    }
    
    if (src->error_count > 0) {
        dest->valid = false;
    }
    
    // Clear source lists to avoid double-free
    src->errors = NULL;
    src->warnings = NULL;
}

// ==================== Error Creation Functions ====================

ValidationError* create_validation_error(ValidationErrorCode code, const char* message,
                                        PathSegment* path, VariableMemPool* pool) {
    ValidationError* error = (ValidationError*)pool_calloc(pool, sizeof(ValidationError));
    error->code = code;
    error->message = string_from_strview(strview_from_cstr(message), pool);
    error->path = path;
    error->expected = NULL;
    error->actual = ITEM_NULL;
    error->suggestions = NULL;
    error->next = NULL;
    return error;
}

// ==================== Suggestion System ====================

List* suggest_similar_names(const char* name, List* available_names, VariableMemPool* pool) {
    List* suggestions = list_new(pool);
    
    if (!name || !available_names) return suggestions;
    
    size_t name_len = strlen(name);
    
    // Simple similarity check based on common prefixes and edit distance
    for (long i = 0; i < available_names->length; i++) {
        Item name_item = list_get(available_names, i);
        
        if (get_type_id(name_item) == LMD_TYPE_STRING) {
            String* candidate = (String*)name_item;
            
            // Check for similar names (simple heuristic)
            if (abs((int)name_len - (int)candidate->len) <= 2) {
                // Calculate simple edit distance (simplified)
                int differences = 0;
                size_t min_len = (name_len < candidate->len) ? name_len : candidate->len;
                
                for (size_t j = 0; j < min_len; j++) {
                    if (name[j] != candidate->chars[j]) {
                        differences++;
                    }
                }
                
                // Add to suggestions if similar enough
                if (differences <= 2) {
                    list_add(suggestions, name_item);
                }
            }
        }
    }
    
    return suggestions;
}

List* suggest_corrections(ValidationError* error, VariableMemPool* pool) {
    List* suggestions = list_new(pool);
    
    if (!error) return suggestions;
    
    // Generate context-specific suggestions based on error type
    switch (error->code) {
        case VALID_ERROR_MISSING_FIELD:
            // Could suggest similar field names
            break;
        case VALID_ERROR_TYPE_MISMATCH:
            // Could suggest type conversions
            break;
        case VALID_ERROR_REFERENCE_ERROR:
            // Could suggest similar reference names
            break;
        default:
            break;
    }
    
    return suggestions;
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
    if (!error) {
        return string_from_strview(strview_from_cstr(""), pool);
    }
    
    char buffer[2048];
    String* path_str = format_validation_path(error->path, pool);
    const char* error_code_name = get_error_code_name(error->code);
    
    snprintf(buffer, sizeof(buffer), "[%s] %s%s%s", 
             error_code_name,
             path_str->chars, 
             (path_str->len > 0) ? ": " : "",
             error->message ? error->message->chars : "Unknown error");
    
    // Add type information if available
    if (error->expected) {
        String* expected_type = format_type_name(error->expected, pool);
        char temp_buffer[512];
        snprintf(temp_buffer, sizeof(temp_buffer), " (expected %s)", expected_type->chars);
        strncat(buffer, temp_buffer, sizeof(buffer) - strlen(buffer) - 1);
    }
    
    // Add suggestions if available
    if (error->suggestions && error->suggestions->length > 0) {
        strncat(buffer, " Did you mean: ", sizeof(buffer) - strlen(buffer) - 1);
        
        for (long i = 0; i < error->suggestions->length && i < 3; i++) {
            Item suggestion = list_get(error->suggestions, i);
            if (get_type_id(suggestion) == LMD_TYPE_STRING) {
                String* suggestion_str = (String*)suggestion;
                if (i > 0) {
                    strncat(buffer, ", ", sizeof(buffer) - strlen(buffer) - 1);
                }
                strncat(buffer, suggestion_str->chars, sizeof(buffer) - strlen(buffer) - 1);
            }
        }
        
        if (error->suggestions->length > 3) {
            strncat(buffer, ", ...", sizeof(buffer) - strlen(buffer) - 1);
        }
        
        strncat(buffer, "?", sizeof(buffer) - strlen(buffer) - 1);
    }
    
    return string_from_strview(strview_from_cstr(buffer), pool);
}

// ==================== Validation Report Generation ====================

String* generate_validation_report(ValidationResult* result, VariableMemPool* pool) {
    if (!result) {
        return string_from_strview(strview_from_cstr("No validation result"), pool);
    }
    
    StrBuf* report = strbuf_new_pooled(pool);
    
    // Header
    if (result->valid) {
        strbuf_append_cstr(report, "✓ Validation successful\n");
    } else {
        strbuf_append_cstr(report, "✗ Validation failed\n");
    }
    
    // Summary
    char summary[256];
    snprintf(summary, sizeof(summary), 
             "Errors: %d, Warnings: %d\n", 
             result->error_count, result->warning_count);
    strbuf_append_cstr(report, summary);
    
    if (result->error_count > 0 || result->warning_count > 0) {
        strbuf_append_cstr(report, "\n");
    }
    
    // Errors
    if (result->error_count > 0) {
        strbuf_append_cstr(report, "Errors:\n");
        ValidationError* error = result->errors;
        int error_num = 1;
        
        while (error) {
            char error_prefix[32];
            snprintf(error_prefix, sizeof(error_prefix), "  %d. ", error_num++);
            strbuf_append_cstr(report, error_prefix);
            
            String* error_str = format_error_with_context(error, pool);
            strbuf_append_string(report, error_str);
            strbuf_append_cstr(report, "\n");
            
            error = error->next;
        }
        strbuf_append_cstr(report, "\n");
    }
    
    // Warnings
    if (result->warning_count > 0) {
        strbuf_append_cstr(report, "Warnings:\n");
        ValidationWarning* warning = result->warnings;
        int warning_num = 1;
        
        while (warning) {
            char warning_prefix[32];
            snprintf(warning_prefix, sizeof(warning_prefix), "  %d. ", warning_num++);
            strbuf_append_cstr(report, warning_prefix);
            
            String* warning_str = format_error_with_context(warning, pool);
            strbuf_append_string(report, warning_str);
            strbuf_append_cstr(report, "\n");
            
            warning = warning->next;
        }
    }
    
    return strbuf_to_string(report);
}

// ==================== JSON Report Generation ====================

String* generate_json_report(ValidationResult* result, VariableMemPool* pool) {
    if (!result) {
        return string_from_strview(strview_from_cstr("{\"error\": \"No validation result\"}"), pool);
    }
    
    StrBuf* json = strbuf_new_pooled(pool);
    
    strbuf_append_cstr(json, "{\n");
    strbuf_append_cstr(json, "  \"valid\": ");
    strbuf_append_cstr(json, result->valid ? "true" : "false");
    strbuf_append_cstr(json, ",\n");
    
    char counts[128];
    snprintf(counts, sizeof(counts), 
             "  \"error_count\": %d,\n  \"warning_count\": %d", 
             result->error_count, result->warning_count);
    strbuf_append_cstr(json, counts);
    
    // Errors array
    if (result->error_count > 0) {
        strbuf_append_cstr(json, ",\n  \"errors\": [\n");
        ValidationError* error = result->errors;
        bool first = true;
        
        while (error) {
            if (!first) {
                strbuf_append_cstr(json, ",\n");
            }
            first = false;
            
            strbuf_append_cstr(json, "    {\n");
            
            // Error code
            char code_str[64];
            snprintf(code_str, sizeof(code_str), 
                     "      \"code\": \"%s\"", get_error_code_name(error->code));
            strbuf_append_cstr(json, code_str);
            
            // Message
            if (error->message) {
                strbuf_append_cstr(json, ",\n      \"message\": \"");
                // Escape JSON string (simplified)
                for (size_t i = 0; i < error->message->len; i++) {
                    char c = error->message->chars[i];
                    if (c == '"' || c == '\\') {
                        strbuf_append_char(json, '\\');
                    }
                    strbuf_append_char(json, c);
                }
                strbuf_append_cstr(json, "\"");
            }
            
            // Path
            if (error->path) {
                String* path_str = format_validation_path(error->path, pool);
                strbuf_append_cstr(json, ",\n      \"path\": \"");
                strbuf_append_string(json, path_str);
                strbuf_append_cstr(json, "\"");
            }
            
            strbuf_append_cstr(json, "\n    }");
            error = error->next;
        }
        
        strbuf_append_cstr(json, "\n  ]");
    }
    
    // Warnings array (similar structure)
    if (result->warning_count > 0) {
        strbuf_append_cstr(json, ",\n  \"warnings\": [\n");
        ValidationWarning* warning = result->warnings;
        bool first = true;
        
        while (warning) {
            if (!first) {
                strbuf_append_cstr(json, ",\n");
            }
            first = false;
            
            strbuf_append_cstr(json, "    {\n");
            
            char code_str[64];
            snprintf(code_str, sizeof(code_str), 
                     "      \"code\": \"%s\"", get_error_code_name(warning->code));
            strbuf_append_cstr(json, code_str);
            
            if (warning->message) {
                strbuf_append_cstr(json, ",\n      \"message\": \"");
                strbuf_append_string(json, warning->message);
                strbuf_append_cstr(json, "\"");
            }
            
            strbuf_append_cstr(json, "\n    }");
            warning = warning->next;
        }
        
        strbuf_append_cstr(json, "\n  ]");
    }
    
    strbuf_append_cstr(json, "\n}");
    
    return strbuf_to_string(json);
}

// ==================== Debug Utilities ====================

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
