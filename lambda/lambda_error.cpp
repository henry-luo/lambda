/**
 * @file lambda_error.cpp
 * @brief Lambda Structured Error Handling Implementation
 */

// Enable POSIX functions like strdup
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "lambda_error.h"
#include "../lib/log.h"
#include "../lib/stringbuf.h"
#include "../lib/arraylist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

// ============================================================================
// Helper: string duplication (portable replacement for strdup)
// ============================================================================

static char* err_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

// ============================================================================
// Error Code Name Lookup
// ============================================================================

typedef struct {
    LambdaErrorCode code;
    const char* name;
    const char* message;
} ErrorCodeInfo;

static const ErrorCodeInfo error_code_table[] = {
    // Success
    {ERR_OK, "OK", "Success"},
    
    // 1xx - Syntax Errors
    {ERR_SYNTAX_ERROR, "SYNTAX_ERROR", "Syntax error"},
    {ERR_UNEXPECTED_TOKEN, "UNEXPECTED_TOKEN", "Unexpected token"},
    {ERR_MISSING_TOKEN, "MISSING_TOKEN", "Missing expected token"},
    {ERR_INVALID_LITERAL, "INVALID_LITERAL", "Invalid literal"},
    {ERR_INVALID_IDENTIFIER, "INVALID_IDENTIFIER", "Invalid identifier"},
    {ERR_UNTERMINATED_STRING, "UNTERMINATED_STRING", "Unterminated string literal"},
    {ERR_UNTERMINATED_COMMENT, "UNTERMINATED_COMMENT", "Unterminated comment"},
    {ERR_INVALID_ESCAPE, "INVALID_ESCAPE", "Invalid escape sequence"},
    {ERR_INVALID_NUMBER, "INVALID_NUMBER", "Invalid number format"},
    {ERR_INVALID_DATETIME, "INVALID_DATETIME", "Invalid datetime literal"},
    {ERR_INVALID_BINARY, "INVALID_BINARY", "Invalid binary literal"},
    {ERR_UNEXPECTED_EOF, "UNEXPECTED_EOF", "Unexpected end of file"},
    {ERR_INVALID_OPERATOR, "INVALID_OPERATOR", "Invalid operator"},
    {ERR_INVALID_ELEMENT_SYNTAX, "INVALID_ELEMENT_SYNTAX", "Invalid element syntax"},
    {ERR_INVALID_MAP_SYNTAX, "INVALID_MAP_SYNTAX", "Invalid map syntax"},
    {ERR_INVALID_ARRAY_SYNTAX, "INVALID_ARRAY_SYNTAX", "Invalid array syntax"},
    {ERR_INVALID_RANGE_SYNTAX, "INVALID_RANGE_SYNTAX", "Invalid range syntax"},
    {ERR_DUPLICATE_PARAMETER, "DUPLICATE_PARAMETER", "Duplicate parameter name"},
    {ERR_INVALID_PARAM_SYNTAX, "INVALID_PARAM_SYNTAX", "Invalid parameter syntax"},
    {ERR_INVALID_TYPE_SYNTAX, "INVALID_TYPE_SYNTAX", "Invalid type syntax"},
    
    // 2xx - Semantic Errors
    {ERR_SEMANTIC_ERROR, "SEMANTIC_ERROR", "Semantic error"},
    {ERR_TYPE_MISMATCH, "TYPE_MISMATCH", "Type mismatch"},
    {ERR_UNDEFINED_VARIABLE, "UNDEFINED_VARIABLE", "Undefined variable"},
    {ERR_UNDEFINED_FUNCTION, "UNDEFINED_FUNCTION", "Undefined function"},
    {ERR_UNDEFINED_TYPE, "UNDEFINED_TYPE", "Undefined type"},
    {ERR_UNDEFINED_FIELD, "UNDEFINED_FIELD", "Undefined field"},
    {ERR_ARGUMENT_COUNT_MISMATCH, "ARGUMENT_COUNT_MISMATCH", "Wrong number of arguments"},
    {ERR_ARGUMENT_TYPE_MISMATCH, "ARGUMENT_TYPE_MISMATCH", "Argument type mismatch"},
    {ERR_RETURN_TYPE_MISMATCH, "RETURN_TYPE_MISMATCH", "Return type mismatch"},
    {ERR_DUPLICATE_DEFINITION, "DUPLICATE_DEFINITION", "Duplicate definition"},
    {ERR_INVALID_ASSIGNMENT, "INVALID_ASSIGNMENT", "Invalid assignment target"},
    {ERR_IMMUTABLE_ASSIGNMENT, "IMMUTABLE_ASSIGNMENT", "Cannot assign to immutable variable"},
    {ERR_INVALID_CALL, "INVALID_CALL", "Cannot call non-function value"},
    {ERR_INVALID_INDEX, "INVALID_INDEX", "Invalid index operation"},
    {ERR_INVALID_MEMBER_ACCESS, "INVALID_MEMBER_ACCESS", "Invalid member access"},
    {ERR_CIRCULAR_DEPENDENCY, "CIRCULAR_DEPENDENCY", "Circular dependency detected"},
    {ERR_IMPORT_NOT_FOUND, "IMPORT_NOT_FOUND", "Module not found"},
    {ERR_IMPORT_ERROR, "IMPORT_ERROR", "Error loading module"},
    {ERR_TRANSPILATION_ERROR, "TRANSPILATION_ERROR", "Transpilation failed"},
    {ERR_JIT_COMPILATION_ERROR, "JIT_COMPILATION_ERROR", "JIT compilation failed"},
    {ERR_RECURSION_DEPTH_EXCEEDED, "RECURSION_DEPTH_EXCEEDED", "Maximum recursion depth exceeded"},
    {ERR_INVALID_EXPR_CONTEXT, "INVALID_EXPR_CONTEXT", "Expression in invalid context"},
    {ERR_MISSING_RETURN, "MISSING_RETURN", "Missing return statement"},
    {ERR_UNREACHABLE_CODE, "UNREACHABLE_CODE", "Unreachable code"},
    {ERR_PROC_IN_FN, "PROC_IN_FN", "Procedural construct in functional context"},
    {ERR_BREAK_OUTSIDE_LOOP, "BREAK_OUTSIDE_LOOP", "Break outside loop"},
    {ERR_CONTINUE_OUTSIDE_LOOP, "CONTINUE_OUTSIDE_LOOP", "Continue outside loop"},
    {ERR_RETURN_OUTSIDE_FUNCTION, "RETURN_OUTSIDE_FUNCTION", "Return outside function"},
    
    // 3xx - Runtime Errors
    {ERR_RUNTIME_ERROR, "RUNTIME_ERROR", "Runtime error"},
    {ERR_NULL_REFERENCE, "NULL_REFERENCE", "Null reference"},
    {ERR_INDEX_OUT_OF_BOUNDS, "INDEX_OUT_OF_BOUNDS", "Index out of bounds"},
    {ERR_KEY_NOT_FOUND, "KEY_NOT_FOUND", "Key not found"},
    {ERR_DIVISION_BY_ZERO, "DIVISION_BY_ZERO", "Division by zero"},
    {ERR_OVERFLOW, "OVERFLOW", "Numeric overflow"},
    {ERR_UNDERFLOW, "UNDERFLOW", "Numeric underflow"},
    {ERR_INVALID_CAST, "INVALID_CAST", "Invalid type conversion"},
    {ERR_STACK_OVERFLOW, "STACK_OVERFLOW", "Stack overflow"},
    {ERR_OUT_OF_MEMORY, "OUT_OF_MEMORY", "Out of memory"},
    {ERR_TIMEOUT, "TIMEOUT", "Execution timeout"},
    {ERR_ASSERTION_FAILED, "ASSERTION_FAILED", "Assertion failed"},
    {ERR_INVALID_OPERATION, "INVALID_OPERATION", "Invalid operation"},
    {ERR_EMPTY_COLLECTION, "EMPTY_COLLECTION", "Operation on empty collection"},
    {ERR_ITERATOR_EXHAUSTED, "ITERATOR_EXHAUSTED", "Iterator exhausted"},
    {ERR_INVALID_REGEX, "INVALID_REGEX", "Invalid regular expression"},
    {ERR_DECIMAL_PRECISION_LOSS, "DECIMAL_PRECISION_LOSS", "Decimal precision loss"},
    {ERR_DATETIME_INVALID, "DATETIME_INVALID", "Invalid datetime operation"},
    {ERR_USER_ERROR, "USER_ERROR", "User error"},
    
    // 4xx - I/O Errors
    {ERR_IO_ERROR, "IO_ERROR", "I/O error"},
    {ERR_FILE_NOT_FOUND, "FILE_NOT_FOUND", "File not found"},
    {ERR_FILE_ACCESS_DENIED, "FILE_ACCESS_DENIED", "Access denied"},
    {ERR_FILE_READ_ERROR, "FILE_READ_ERROR", "File read error"},
    {ERR_FILE_WRITE_ERROR, "FILE_WRITE_ERROR", "File write error"},
    {ERR_NETWORK_ERROR, "NETWORK_ERROR", "Network error"},
    {ERR_NETWORK_TIMEOUT, "NETWORK_TIMEOUT", "Network timeout"},
    {ERR_PARSE_ERROR, "PARSE_ERROR", "Parse error"},
    {ERR_FORMAT_ERROR, "FORMAT_ERROR", "Format error"},
    {ERR_ENCODING_ERROR, "ENCODING_ERROR", "Encoding error"},
    {ERR_INVALID_URL, "INVALID_URL", "Invalid URL"},
    {ERR_HTTP_ERROR, "HTTP_ERROR", "HTTP error"},
    
    // 5xx - Internal Errors
    {ERR_INTERNAL_ERROR, "INTERNAL_ERROR", "Internal error"},
    {ERR_NOT_IMPLEMENTED, "NOT_IMPLEMENTED", "Not implemented"},
    {ERR_INVALID_STATE, "INVALID_STATE", "Invalid state"},
    {ERR_MEMORY_CORRUPTION, "MEMORY_CORRUPTION", "Memory corruption detected"},
    {ERR_TYPE_SYSTEM_ERROR, "TYPE_SYSTEM_ERROR", "Type system error"},
    {ERR_POOL_EXHAUSTED, "POOL_EXHAUSTED", "Memory pool exhausted"},
};

static const int error_code_count = sizeof(error_code_table) / sizeof(error_code_table[0]);

const char* err_code_name(LambdaErrorCode code) {
    for (int i = 0; i < error_code_count; i++) {
        if (error_code_table[i].code == code) {
            return error_code_table[i].name;
        }
    }
    return "UNKNOWN_ERROR";
}

const char* err_code_message(LambdaErrorCode code) {
    for (int i = 0; i < error_code_count; i++) {
        if (error_code_table[i].code == code) {
            return error_code_table[i].message;
        }
    }
    return "Unknown error";
}

const char* err_category_name(LambdaErrorCode code) {
    if (ERR_IS_SYNTAX(code)) return "Syntax";
    if (ERR_IS_SEMANTIC(code)) return "Semantic";
    if (ERR_IS_RUNTIME(code)) return "Runtime";
    if (ERR_IS_IO(code)) return "I/O";
    if (ERR_IS_INTERNAL(code)) return "Internal";
    return "Unknown";
}

// ============================================================================
// Error Creation
// ============================================================================

LambdaError* err_create(LambdaErrorCode code, const char* message, SourceLocation* location) {
    LambdaError* error = (LambdaError*)calloc(1, sizeof(LambdaError));
    if (!error) return NULL;
    
    error->code = code;
    error->message = message ? err_strdup(message) : err_strdup(err_code_message(code));
    
    if (location) {
        error->location = *location;
    }
    
    return error;
}

LambdaError* err_createf(LambdaErrorCode code, SourceLocation* location, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    return err_create(code, buffer, location);
}

LambdaError* err_create_simple(LambdaErrorCode code, const char* message) {
    return err_create(code, message, NULL);
}

// ============================================================================
// Error Enrichment
// ============================================================================

void err_set_location(LambdaError* error, const char* file, uint32_t line, uint32_t col) {
    if (!error) return;
    error->location.file = file;
    error->location.line = line;
    error->location.column = col;
}

void err_add_help(LambdaError* error, const char* help) {
    if (!error || !help) return;
    if (error->help) free(error->help);
    error->help = err_strdup(help);
}

void err_set_cause(LambdaError* error, LambdaError* cause) {
    if (!error) return;
    error->cause = cause;
}

void err_set_stack_trace(LambdaError* error, StackFrame* trace) {
    if (!error) return;
    error->stack_trace = trace;
}

// ============================================================================
// Source Location Helpers
// ============================================================================

SourceLocation src_loc(const char* file, uint32_t line, uint32_t col) {
    SourceLocation loc = {0};
    loc.file = file;
    loc.line = line;
    loc.column = col;
    loc.end_line = line;
    loc.end_column = col;
    return loc;
}

SourceLocation src_loc_span(const char* file, uint32_t line, uint32_t col,
                            uint32_t end_line, uint32_t end_col) {
    SourceLocation loc = {0};
    loc.file = file;
    loc.line = line;
    loc.column = col;
    loc.end_line = end_line;
    loc.end_column = end_col;
    return loc;
}

// ============================================================================
// Source Context Extraction
// ============================================================================

// count lines in source string
int err_get_source_line_count(const char* source) {
    if (!source) return 0;
    
    int count = 1;  // at least one line
    while (*source) {
        if (*source == '\n') count++;
        source++;
    }
    return count;
}

// extract a single line from source (caller must free result)
char* err_get_source_line(const char* source, uint32_t line_number) {
    if (!source || line_number == 0) return NULL;
    
    uint32_t current_line = 1;
    const char* line_start = source;
    
    // find the start of the requested line
    while (*source && current_line < line_number) {
        if (*source == '\n') {
            current_line++;
            line_start = source + 1;
        }
        source++;
    }
    
    if (current_line != line_number) return NULL;  // line not found
    
    // find the end of the line
    const char* line_end = line_start;
    while (*line_end && *line_end != '\n') {
        line_end++;
    }
    
    // copy the line
    size_t len = line_end - line_start;
    char* result = (char*)malloc(len + 1);
    if (result) {
        memcpy(result, line_start, len);
        result[len] = '\0';
    }
    return result;
}

// extract source context around the error location
void err_extract_context(LambdaError* error, const char* source, int context_lines) {
    if (!error || !source) return;
    
    // store source reference for later formatting
    error->location.source = source;
    
    // context_lines parameter is stored for formatting (unused for now, formatting uses it directly)
    (void)context_lines;
}

// ============================================================================
// Stack Trace Capture
// ============================================================================

#if defined(__APPLE__) || defined(__linux__)

StackFrame* err_capture_stack_trace(void* debug_info_list, int max_frames) {
    void* frames[64];
    int frame_count = backtrace(frames, 64);
    
    if (frame_count <= 0) return NULL;
    
    // limit frames if requested
    if (max_frames > 0 && frame_count > max_frames) {
        frame_count = max_frames;
    }
    
    StackFrame* result = NULL;
    StackFrame** tail = &result;
    
    // skip the first few frames (err_capture_stack_trace, runtime_error, etc.)
    int skip_frames = 2;
    
    for (int i = skip_frames; i < frame_count; i++) {
        void* addr = frames[i];
        
        // look up in debug info table if provided
        FuncDebugInfo* info = NULL;
        if (debug_info_list) {
            // cast to ArrayList and search
            ArrayList* list = (ArrayList*)debug_info_list;
            for (int j = 0; j < list->length; j++) {
                FuncDebugInfo* candidate = (FuncDebugInfo*)list->data[j];
                if (addr >= candidate->native_addr_start && addr < candidate->native_addr_end) {
                    info = candidate;
                    break;
                }
            }
        }
        
        // create stack frame
        StackFrame* frame = (StackFrame*)calloc(1, sizeof(StackFrame));
        if (!frame) break;
        
        if (info) {
            frame->function_name = info->lambda_func_name;
            frame->location.file = info->source_file;
            frame->location.line = info->source_line;
        } else {
            // try to get symbol name from backtrace_symbols
            char** symbols = backtrace_symbols(&addr, 1);
            if (symbols && symbols[0]) {
                // extract function name from symbol string
                // format varies by platform, typically: "module(func+offset) [addr]"
                frame->function_name = err_strdup(symbols[0]);
                free(symbols);
            } else {
                frame->function_name = "<unknown>";
            }
        }
        
        *tail = frame;
        tail = &frame->next;
    }
    
    return result;
}

#elif defined(_WIN32)

StackFrame* err_capture_stack_trace(void* debug_info_list, int max_frames) {
    void* frames[64];
    USHORT frame_count = CaptureStackBackTrace(0, 64, frames, NULL);
    
    if (frame_count <= 0) return NULL;
    
    if (max_frames > 0 && frame_count > max_frames) {
        frame_count = (USHORT)max_frames;
    }
    
    StackFrame* result = NULL;
    StackFrame** tail = &result;
    
    int skip_frames = 2;
    
    for (int i = skip_frames; i < frame_count; i++) {
        void* addr = frames[i];
        
        FuncDebugInfo* info = NULL;
        if (debug_info_list) {
            ArrayList* list = (ArrayList*)debug_info_list;
            for (int j = 0; j < list->length; j++) {
                FuncDebugInfo* candidate = (FuncDebugInfo*)list->data[j];
                if (addr >= candidate->native_addr_start && addr < candidate->native_addr_end) {
                    info = candidate;
                    break;
                }
            }
        }
        
        StackFrame* frame = (StackFrame*)calloc(1, sizeof(StackFrame));
        if (!frame) break;
        
        if (info) {
            frame->function_name = info->lambda_func_name;
            frame->location.file = info->source_file;
            frame->location.line = info->source_line;
        } else {
            frame->function_name = "<unknown>";
        }
        
        *tail = frame;
        tail = &frame->next;
    }
    
    return result;
}

#else

StackFrame* err_capture_stack_trace(void* debug_info_list, int max_frames) {
    (void)debug_info_list;
    (void)max_frames;
    return NULL;  // unsupported platform
}

#endif

// ============================================================================
// Error Output
// ============================================================================

char* err_format(LambdaError* error) {
    if (!error) return err_strdup("(null error)");
    
    char buffer[4096];
    int pos = 0;
    
    // location prefix
    if (error->location.file && error->location.line > 0) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s:%u:%u: ", 
            error->location.file,
            error->location.line,
            error->location.column > 0 ? error->location.column : 1);
    }
    
    // error type and code
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "error[E%d]: ", error->code);
    
    // message
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s", 
        error->message ? error->message : err_code_message(error->code));
    
    // help text
    if (error->help) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\n    = help: %s", error->help);
    }
    
    return err_strdup(buffer);
}

char* err_format_with_context(LambdaError* error, int context_lines) {
    if (!error) return err_strdup("(null error)");
    
    char buffer[8192];
    int pos = 0;
    
    // location prefix
    if (error->location.file && error->location.line > 0) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s:%u:%u: ", 
            error->location.file,
            error->location.line,
            error->location.column > 0 ? error->location.column : 1);
    }
    
    // error type and code
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "error[E%d]: ", error->code);
    
    // message
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s\n",
        error->message ? error->message : err_code_message(error->code));
    
    // source context (if available)
    if (error->location.source && error->location.line > 0) {
        const char* src = error->location.source;
        uint32_t target_line = error->location.line;
        uint32_t start_line = (target_line > (uint32_t)context_lines) ? (target_line - context_lines) : 1;
        uint32_t end_line = target_line + context_lines;
        
        // calculate max line number width for alignment
        int line_width = 1;
        uint32_t temp = end_line;
        while (temp >= 10) { temp /= 10; line_width++; }
        
        // empty gutter line
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%*s |\n", line_width, "");
        
        // extract and print each context line
        for (uint32_t line_num = start_line; line_num <= end_line && (size_t)pos < sizeof(buffer) - 256; line_num++) {
            char* line_text = err_get_source_line(src, line_num);
            if (!line_text) break;  // past end of source
            
            // print line number and content
            if (line_num == target_line) {
                pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%*u | %s\n", 
                    line_width, line_num, line_text);
                
                // print caret indicator for the error column/span
                pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%*s | ", line_width, "");
                
                uint32_t col = error->location.column > 0 ? error->location.column : 1;
                uint32_t end_col = error->location.end_column > 0 ? error->location.end_column : col;
                if (end_col < col) end_col = col;
                
                // span length (at least 1)
                uint32_t span_len = end_col - col + 1;
                if (span_len > 20) span_len = 20;  // limit caret length
                if (span_len < 1) span_len = 1;
                
                // spaces before carets
                for (uint32_t i = 1; i < col && (size_t)pos < sizeof(buffer) - 32; i++) {
                    buffer[pos++] = ' ';
                }
                
                // carets
                for (uint32_t i = 0; i < span_len && (size_t)pos < sizeof(buffer) - 16; i++) {
                    buffer[pos++] = '^';
                }
                buffer[pos++] = '\n';
            } else {
                pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%*u | %s\n", 
                    line_width, line_num, line_text);
            }
            
            free(line_text);
        }
    }
    
    // help text
    if (error->help) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "    = help: %s\n", error->help);
    }
    
    // stack trace
    if (error->stack_trace) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\nStack trace:\n");
        StackFrame* frame = error->stack_trace;
        int depth = 0;
        while (frame && (size_t)pos < sizeof(buffer) - 100) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos, "  %d: at %s", depth,
                frame->function_name ? frame->function_name : "<unknown>");
            if (frame->location.file) {
                pos += snprintf(buffer + pos, sizeof(buffer) - pos, " (%s:%u)", 
                    frame->location.file, frame->location.line);
            }
            pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\n");
            frame = frame->next;
            depth++;
        }
    }
    
    // caused by
    if (error->cause) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\nCaused by:\n  ");
        char* cause_str = err_format(error->cause);
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s\n", cause_str);
        free(cause_str);
    }
    
    return err_strdup(buffer);
}

void err_print(LambdaError* error) {
    char* formatted = err_format_with_context(error, 3);
    fprintf(stderr, "%s", formatted);
    free(formatted);
}

void err_print_stack_trace(StackFrame* trace) {
    if (!trace) return;
    
    fprintf(stderr, "Stack trace:\n");
    int depth = 0;
    while (trace) {
        fprintf(stderr, "  %d: at %s", depth,
            trace->function_name ? trace->function_name : "<unknown>");
        if (trace->location.file) {
            fprintf(stderr, " (%s:%u)", trace->location.file, trace->location.line);
        }
        fprintf(stderr, "\n");
        trace = trace->next;
        depth++;
    }
}

// ============================================================================
// Error Cleanup
// ============================================================================

void err_free_stack_trace(StackFrame* trace) {
    while (trace) {
        StackFrame* next = trace->next;
        // note: function_name may be strdup'd or static, need to track ownership
        free(trace);
        trace = next;
    }
}

void err_free(LambdaError* error) {
    if (!error) return;
    
    if (error->message) free(error->message);
    if (error->help) free(error->help);
    if (error->stack_trace) err_free_stack_trace(error->stack_trace);
    if (error->cause) err_free(error->cause);
    
    free(error);
}
