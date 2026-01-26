/**
 * @file lambda_error.cpp
 * @brief Lambda Structured Error Handling Implementation
 */

// Enable POSIX/GNU functions before any system includes
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_GNU_SOURCE) && (defined(__linux__) || defined(__APPLE__))
#define _GNU_SOURCE
#endif

// Platform-specific includes (must come before other includes for feature macros)
#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#include <pthread.h>

// dladdr and Dl_info for resolving C function names from addresses
extern "C" {
    typedef struct {
        const char  *dli_fname;  // Pathname of shared object
        void        *dli_fbase;  // Base address of shared object
        const char  *dli_sname;  // Name of nearest symbol
        void        *dli_saddr;  // Address of nearest symbol
    } Dl_info;
    
    int dladdr(const void *addr, Dl_info *info);
}
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

#include "lambda_error.h"
#include "../lib/log.h"
#include "../lib/stringbuf.h"
#include "../lib/arraylist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// Note: MIR headers removed - build_debug_info_table is now in mir.c

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
// Debug Info Table for Native Stack Walking
// ============================================================================

// Simple dynamic array for debug info (avoids ArrayList API complexity)
typedef struct {
    FuncDebugInfo** items;
    size_t length;
    size_t capacity;
} DebugInfoList;

// Note: build_debug_info_table is implemented in mir.c because it needs MIR APIs
// which are only linked into the main lambda executable.

// Look up debug info for a native address using binary search
// Returns NULL if address is not in any Lambda function
FuncDebugInfo* lookup_debug_info(void* debug_info_list, void* addr) {
    if (!debug_info_list || !addr) return NULL;
    
    DebugInfoList* list = (DebugInfoList*)debug_info_list;
    if (list->length == 0) return NULL;
    
    // Binary search: find the function whose range contains addr
    int lo = 0;
    int hi = (int)list->length - 1;
    
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        FuncDebugInfo* info = list->items[mid];
        
        if (addr < info->native_addr_start) {
            hi = mid - 1;
        } else if (addr >= info->native_addr_end) {
            lo = mid + 1;
        } else {
            // Found: addr is within [start, end)
            return info;
        }
    }
    
    return NULL;  // Not found: address is in runtime/external code
}

// Free debug info table
void free_debug_info_table(void* debug_info_list) {
    if (!debug_info_list) return;
    
    DebugInfoList* list = (DebugInfoList*)debug_info_list;
    for (size_t i = 0; i < list->length; i++) {
        free(list->items[i]);
    }
    free(list->items);
    free(list);
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
// Stack Trace Capture (Manual Frame Pointer Walking)
// ============================================================================
//
// MIR JIT generates proper frame pointer chains on ARM64 and x86-64.
// The standard `backtrace()` doesn't work with JIT code because:
//   - No DWARF/eh_frame unwind info
//   - macOS code signing issues with JIT memory
//
// Solution: Walk the frame pointer chain manually and look up addresses
// in the debug info table built from MIR function addresses.
//
// ARM64 Stack Layout (per MIR prologue):
//   [FP + 8]  = saved LR (return address)
//   [FP + 0]  = saved FP (previous frame pointer)
//
// x86-64 Stack Layout:
//   [RBP + 8] = return address  
//   [RBP + 0] = saved RBP (previous frame pointer)
// ============================================================================

#if defined(__aarch64__) || defined(_M_ARM64)

// Read current frame pointer (ARM64: x29)
static inline void* get_frame_pointer(void) {
    void* fp;
    __asm__ volatile("mov %0, x29" : "=r"(fp));
    return fp;
}

#elif defined(__x86_64__) || defined(_M_X64)

// Read current frame pointer (x86-64: rbp)
static inline void* get_frame_pointer(void) {
    void* fp;
#if defined(_MSC_VER)
    // MSVC doesn't support inline asm for x64, use intrinsic
    fp = (void*)_AddressOfReturnAddress();
    fp = *((void**)fp - 1);  // approximate
#else
    __asm__ volatile("mov %%rbp, %0" : "=r"(fp));
#endif
    return fp;
}

#else

static inline void* get_frame_pointer(void) {
    return NULL;  // unsupported architecture
}

#endif

// Get stack bounds for the current thread (to validate FP chain)
static void get_stack_bounds(void** stack_top, void** stack_bottom) {
#if defined(__APPLE__)
    // macOS: pthread API
    pthread_t self = pthread_self();
    *stack_bottom = pthread_get_stackaddr_np(self);
    size_t stack_size = pthread_get_stacksize_np(self);
    *stack_top = (char*)*stack_bottom - stack_size;
#elif defined(__linux__)
    // Linux: use pthread_attr_getstack
    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);
    size_t stack_size;
    pthread_attr_getstack(&attr, stack_top, &stack_size);
    *stack_bottom = (char*)*stack_top + stack_size;
    pthread_attr_destroy(&attr);
#else
    // Fallback: use heuristics
    *stack_top = (void*)0x1000;  // avoid NULL region
    *stack_bottom = (void*)((uintptr_t)-1);
#endif
}

// Walk the frame pointer chain and build stack trace
StackFrame* err_capture_stack_trace(void* debug_info_list, int max_frames) {
    if (max_frames <= 0) max_frames = 64;
    
    void* fp = get_frame_pointer();
    if (!fp) {
        log_debug("err_capture_stack_trace: get_frame_pointer returned NULL");
        return NULL;
    }
    
    // Get stack bounds for validation
    void* stack_top = NULL;
    void* stack_bottom = NULL;
    get_stack_bounds(&stack_top, &stack_bottom);
    
    log_debug("err_capture_stack_trace: starting FP walk from %p, stack=[%p, %p], debug_info=%p",
              fp, stack_top, stack_bottom, debug_info_list);
    
    StackFrame* result = NULL;
    StackFrame** tail = &result;
    int depth = 0;
    int total_frames_found = 0;
    
    void** frame_ptr = (void**)fp;
    
    while (frame_ptr != NULL && depth < max_frames) {
        // Validate frame pointer is within stack bounds and aligned
        uintptr_t fp_addr = (uintptr_t)frame_ptr;
        
        // Check alignment (8-byte on 64-bit)
        if ((fp_addr & 0x7) != 0) {
            log_debug("err_capture_stack_trace: misaligned FP %p at depth %d", frame_ptr, depth);
            break;
        }
        
        // Check within stack bounds
        if ((void*)frame_ptr < stack_top || (void*)frame_ptr >= stack_bottom) {
            log_debug("err_capture_stack_trace: FP %p outside stack bounds at depth %d", frame_ptr, depth);
            break;
        }
        
        // Read return address (at FP+8 on ARM64/x86-64)
        void* return_addr = frame_ptr[1];
        
        // Read previous frame pointer (at FP+0)
        void* prev_fp = frame_ptr[0];
        
        log_debug("err_capture_stack_trace: depth=%d fp=%p return_addr=%p prev_fp=%p",
                  depth, frame_ptr, return_addr, prev_fp);
        
        // Look up return address in debug info table (Lambda JIT functions)
        FuncDebugInfo* info = lookup_debug_info(debug_info_list, return_addr);
        
        if (info) {
            // Found a Lambda function
            log_debug("err_capture_stack_trace: found Lambda func '%s' at %p",
                      info->lambda_func_name, return_addr);
            
            StackFrame* frame = (StackFrame*)calloc(1, sizeof(StackFrame));
            if (!frame) break;
            
            frame->function_name = info->lambda_func_name;
            frame->location.file = info->source_file;
            frame->location.line = info->source_line;
            frame->is_native = false;
            frame->next = NULL;
            
            *tail = frame;
            tail = &frame->next;
            total_frames_found++;
            depth++;
        }
#if defined(__APPLE__) || defined(__linux__)
        else {
            // Try to resolve C function name using dladdr
            Dl_info dl_info;
            if (dladdr(return_addr, &dl_info) && dl_info.dli_sname) {
                const char* name = dl_info.dli_sname;
                
                // Filter functions to include:
                // - fn_* (Lambda sys funcs)
                // Skip:
                // - set_runtime_error, err_* (error machinery)
                // - start, main (system entry points - except Lambda main)
                // - internal runtime functions
                
                bool is_lambda_sys_func = (strncmp(name, "fn_", 3) == 0);
                bool is_error_machinery = (strncmp(name, "set_runtime_error", 17) == 0) ||
                                          (strncmp(name, "err_", 4) == 0);
                bool is_system_entry = (strcmp(name, "start") == 0) ||
                                       (strcmp(name, "main") == 0);  // C main, not Lambda main
                
                if (is_lambda_sys_func && !is_error_machinery) {
                    log_debug("err_capture_stack_trace: found C func '%s' at %p",
                              name, return_addr);
                    
                    StackFrame* frame = (StackFrame*)calloc(1, sizeof(StackFrame));
                    if (!frame) break;
                    
                    frame->function_name = err_strdup(name);
                    frame->location.file = NULL;
                    frame->location.line = 0;
                    frame->is_native = true;
                    frame->next = NULL;
                    
                    *tail = frame;
                    tail = &frame->next;
                    total_frames_found++;
                    depth++;
                } else {
                    log_debug("err_capture_stack_trace: skipping C func '%s' at %p",
                              name, return_addr);
                }
            }
        }
#endif
        
        // Follow chain: check prev_fp is valid (must be higher address on stack)
        if (prev_fp == NULL) {
            log_debug("err_capture_stack_trace: reached NULL FP at depth %d", depth);
            break;
        }
        
        // Stack grows down, so prev_fp should be > current fp
        if ((uintptr_t)prev_fp <= (uintptr_t)frame_ptr) {
            log_debug("err_capture_stack_trace: FP chain going wrong direction at depth %d", depth);
            break;
        }
        
        frame_ptr = (void**)prev_fp;
    }
    
    log_info("err_capture_stack_trace: captured %d frames (Lambda + C)", total_frames_found);
    return result;
}

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
    
    // location prefix - validate file pointer before use
    if (error->location.file && error->location.line > 0) {
        // sanity check: file pointer should be in a reasonable memory range
        // and first few characters should be printable ASCII or common UTF-8
        const char* file = error->location.file;
        uintptr_t file_addr = (uintptr_t)file;
        bool file_valid = (file_addr > 0x10000) && (file[0] >= 0x20 || file[0] == '\0');
        if (file_valid) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s:%u:%u: ", 
                error->location.file,
                error->location.line,
                error->location.column > 0 ? error->location.column : 1);
        }
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
            const char* name = frame->function_name ? frame->function_name : "<unknown>";
            if (frame->is_native) {
                pos += snprintf(buffer + pos, sizeof(buffer) - pos, "  %d: at %s [native]", depth, name);
            } else {
                pos += snprintf(buffer + pos, sizeof(buffer) - pos, "  %d: at %s", depth, name);
            }
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
        const char* name = trace->function_name ? trace->function_name : "<unknown>";
        if (trace->is_native) {
            fprintf(stderr, "  %d: at %s [native]", depth, name);
        } else {
            fprintf(stderr, "  %d: at %s", depth, name);
        }
        if (trace->location.file) {
            fprintf(stderr, " (%s:%u)", trace->location.file, trace->location.line);
        }
        fprintf(stderr, "\n");
        trace = trace->next;
        depth++;
    }
}

// ============================================================================
// JSON Error Output
// ============================================================================

// helper to escape JSON string
static void json_escape_string(char* dest, size_t dest_size, const char* src) {
    if (!src) { dest[0] = '\0'; return; }
    
    size_t di = 0;
    while (*src && di < dest_size - 2) {
        char c = *src++;
        switch (c) {
            case '"':  if (di < dest_size - 3) { dest[di++] = '\\'; dest[di++] = '"'; } break;
            case '\\': if (di < dest_size - 3) { dest[di++] = '\\'; dest[di++] = '\\'; } break;
            case '\n': if (di < dest_size - 3) { dest[di++] = '\\'; dest[di++] = 'n'; } break;
            case '\r': if (di < dest_size - 3) { dest[di++] = '\\'; dest[di++] = 'r'; } break;
            case '\t': if (di < dest_size - 3) { dest[di++] = '\\'; dest[di++] = 't'; } break;
            default:   dest[di++] = c; break;
        }
    }
    dest[di] = '\0';
}

char* err_format_json(LambdaError* error) {
    if (!error) return err_strdup("null");
    
    char buffer[4096];
    int pos = 0;
    
    // escape message and help for JSON
    char escaped_msg[1024] = {0};
    char escaped_help[512] = {0};
    char escaped_file[256] = {0};
    
    if (error->message) json_escape_string(escaped_msg, sizeof(escaped_msg), error->message);
    if (error->help) json_escape_string(escaped_help, sizeof(escaped_help), error->help);
    if (error->location.file) json_escape_string(escaped_file, sizeof(escaped_file), error->location.file);
    
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "{\n");
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "  \"code\": %d,\n", error->code);
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "  \"name\": \"%s\",\n", err_code_name(error->code));
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "  \"category\": \"%s\",\n", err_category_name(error->code));
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "  \"severity\": \"error\",\n");
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "  \"message\": \"%s\",\n", escaped_msg);
    
    // location object
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "  \"location\": {\n");
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "    \"file\": \"%s\",\n", 
        error->location.file ? escaped_file : "");
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "    \"line\": %u,\n", error->location.line);
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "    \"column\": %u,\n", error->location.column);
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "    \"endLine\": %u,\n", 
        error->location.end_line > 0 ? error->location.end_line : error->location.line);
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "    \"endColumn\": %u\n", 
        error->location.end_column > 0 ? error->location.end_column : error->location.column);
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "  }");
    
    // optional help
    if (error->help) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, ",\n  \"help\": \"%s\"", escaped_help);
    }
    
    // stack trace (if present)
    if (error->stack_trace) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, ",\n  \"stackTrace\": [\n");
        StackFrame* frame = error->stack_trace;
        bool first = true;
        while (frame && (size_t)pos < sizeof(buffer) - 200) {
            if (!first) pos += snprintf(buffer + pos, sizeof(buffer) - pos, ",\n");
            first = false;
            
            char escaped_fn[256] = {0};
            char escaped_frame_file[256] = {0};
            if (frame->function_name) json_escape_string(escaped_fn, sizeof(escaped_fn), frame->function_name);
            if (frame->location.file) json_escape_string(escaped_frame_file, sizeof(escaped_frame_file), frame->location.file);
            
            pos += snprintf(buffer + pos, sizeof(buffer) - pos, 
                "    {\"function\": \"%s\", \"file\": \"%s\", \"line\": %u}",
                escaped_fn, escaped_frame_file, frame->location.line);
            frame = frame->next;
        }
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\n  ]");
    }
    
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\n}");
    
    return err_strdup(buffer);
}

char* err_format_json_array(LambdaError** errors, int count) {
    if (!errors || count == 0) return err_strdup("{\"errors\": [], \"errorCount\": 0}");
    
    // estimate buffer size needed
    size_t buf_size = 256 + count * 2048;
    char* buffer = (char*)malloc(buf_size);
    if (!buffer) return err_strdup("{\"errors\": [], \"errorCount\": 0, \"error\": \"out of memory\"}");
    
    int pos = 0;
    pos += snprintf(buffer + pos, buf_size - pos, "{\n  \"errors\": [\n");
    
    for (int i = 0; i < count && (size_t)pos < buf_size - 100; i++) {
        char* err_json = err_format_json(errors[i]);
        if (i > 0) pos += snprintf(buffer + pos, buf_size - pos, ",\n");
        
        // indent each line of the error JSON
        const char* line = err_json;
        while (*line && (size_t)pos < buf_size - 100) {
            pos += snprintf(buffer + pos, buf_size - pos, "    ");
            while (*line && *line != '\n' && (size_t)pos < buf_size - 10) {
                buffer[pos++] = *line++;
            }
            if (*line == '\n') {
                buffer[pos++] = '\n';
                line++;
            }
        }
        free(err_json);
    }
    
    pos += snprintf(buffer + pos, buf_size - pos, "\n  ],\n");
    pos += snprintf(buffer + pos, buf_size - pos, "  \"errorCount\": %d\n", count);
    pos += snprintf(buffer + pos, buf_size - pos, "}");
    
    return buffer;
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
