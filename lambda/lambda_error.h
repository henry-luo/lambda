/**
 * @file lambda_error.h
 * @brief Lambda Structured Error Handling System
 * 
 * Provides a comprehensive error code system with source location tracking
 * and stack trace support for better debugging and error reporting.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Error Code Ranges
// ============================================================================

#define ERR_SYNTAX_BASE    100
#define ERR_SEMANTIC_BASE  200
#define ERR_RUNTIME_BASE   300
#define ERR_IO_BASE        400
#define ERR_INTERNAL_BASE  500

// Category macros
#define ERR_IS_SYNTAX(code)    ((code) >= 100 && (code) < 200)
#define ERR_IS_SEMANTIC(code)  ((code) >= 200 && (code) < 300)
#define ERR_IS_RUNTIME(code)   ((code) >= 300 && (code) < 400)
#define ERR_IS_IO(code)        ((code) >= 400 && (code) < 500)
#define ERR_IS_INTERNAL(code)  ((code) >= 500 && (code) < 600)

// ============================================================================
// Error Codes
// ============================================================================

typedef enum LambdaErrorCode {
    // Success
    ERR_OK = 0,
    
    // -------------------------------------------------------------------------
    // 1xx - Syntax Errors (parsing, lexical)
    // -------------------------------------------------------------------------
    ERR_SYNTAX_ERROR = 100,           // generic syntax error
    ERR_UNEXPECTED_TOKEN = 101,       // unexpected token encountered
    ERR_MISSING_TOKEN = 102,          // expected token missing (e.g., `)`, `}`)
    ERR_INVALID_LITERAL = 103,        // malformed literal
    ERR_INVALID_IDENTIFIER = 104,     // invalid identifier format
    ERR_UNTERMINATED_STRING = 105,    // string literal not closed
    ERR_UNTERMINATED_COMMENT = 106,   // comment block not closed
    ERR_INVALID_ESCAPE = 107,         // invalid escape sequence
    ERR_INVALID_NUMBER = 108,         // invalid numeric literal
    ERR_INVALID_DATETIME = 109,       // invalid datetime literal
    ERR_INVALID_BINARY = 110,         // invalid binary literal
    ERR_UNEXPECTED_EOF = 111,         // unexpected end of file
    ERR_INVALID_OPERATOR = 112,       // invalid operator
    ERR_INVALID_ELEMENT_SYNTAX = 113, // malformed element `<tag ...>`
    ERR_INVALID_MAP_SYNTAX = 114,     // malformed map `{...}`
    ERR_INVALID_ARRAY_SYNTAX = 115,   // malformed array `[...]`
    ERR_INVALID_RANGE_SYNTAX = 116,   // malformed range expression
    ERR_DUPLICATE_PARAMETER = 117,    // duplicate parameter name
    ERR_INVALID_PARAM_SYNTAX = 118,   // malformed function parameter
    ERR_INVALID_TYPE_SYNTAX = 119,    // malformed type annotation
    
    // -------------------------------------------------------------------------
    // 2xx - Semantic/Compilation Errors
    // -------------------------------------------------------------------------
    ERR_SEMANTIC_ERROR = 200,         // generic semantic error
    ERR_TYPE_MISMATCH = 201,          // type incompatibility
    ERR_UNDEFINED_VARIABLE = 202,     // reference to undefined variable
    ERR_UNDEFINED_FUNCTION = 203,     // reference to undefined function
    ERR_UNDEFINED_TYPE = 204,         // reference to undefined type
    ERR_UNDEFINED_FIELD = 205,        // reference to undefined field
    ERR_ARGUMENT_COUNT_MISMATCH = 206,// wrong number of function arguments
    ERR_ARGUMENT_TYPE_MISMATCH = 207, // function argument type incompatible
    ERR_RETURN_TYPE_MISMATCH = 208,   // function return type incompatible
    ERR_DUPLICATE_DEFINITION = 209,   // duplicate function/variable/type
    ERR_INVALID_ASSIGNMENT = 210,     // invalid assignment target
    ERR_IMMUTABLE_ASSIGNMENT = 211,   // assignment to immutable variable
    ERR_INVALID_CALL = 212,           // calling non-function value
    ERR_INVALID_INDEX = 213,          // invalid index access
    ERR_INVALID_MEMBER_ACCESS = 214,  // invalid member access
    ERR_CIRCULAR_DEPENDENCY = 215,    // circular import/type dependency
    ERR_IMPORT_NOT_FOUND = 216,       // module import not found
    ERR_IMPORT_ERROR = 217,           // error loading imported module
    ERR_TRANSPILATION_ERROR = 218,    // generic transpilation failure
    ERR_JIT_COMPILATION_ERROR = 219,  // MIR JIT compilation failure
    ERR_RECURSION_DEPTH_EXCEEDED = 220,// max AST recursion depth exceeded
    ERR_INVALID_EXPR_CONTEXT = 221,   // expression in invalid context
    ERR_MISSING_RETURN = 222,         // missing return in typed function
    ERR_UNREACHABLE_CODE = 223,       // code after return (warning)
    ERR_PROC_IN_FN = 224,             // procedural construct in fn
    ERR_BREAK_OUTSIDE_LOOP = 225,     // break used outside loop
    ERR_CONTINUE_OUTSIDE_LOOP = 226,  // continue used outside loop
    ERR_RETURN_OUTSIDE_FUNCTION = 227,// return used outside function
    
    // -------------------------------------------------------------------------
    // 3xx - Runtime Errors
    // -------------------------------------------------------------------------
    ERR_RUNTIME_ERROR = 300,          // generic runtime error
    ERR_NULL_REFERENCE = 301,         // null dereference
    ERR_INDEX_OUT_OF_BOUNDS = 302,    // array/list index out of range
    ERR_KEY_NOT_FOUND = 303,          // map key not found
    ERR_DIVISION_BY_ZERO = 304,       // division or modulo by zero
    ERR_OVERFLOW = 305,               // numeric overflow
    ERR_UNDERFLOW = 306,              // numeric underflow
    ERR_INVALID_CAST = 307,           // invalid type cast/conversion
    ERR_STACK_OVERFLOW = 308,         // recursion/call stack overflow
    ERR_OUT_OF_MEMORY = 309,          // memory allocation failure
    ERR_TIMEOUT = 310,                // execution timeout exceeded
    ERR_ASSERTION_FAILED = 311,       // assertion failure
    ERR_INVALID_OPERATION = 312,      // operation not valid for type
    ERR_EMPTY_COLLECTION = 313,       // operation on empty collection
    ERR_ITERATOR_EXHAUSTED = 314,     // iterator has no more elements
    ERR_INVALID_REGEX = 315,          // invalid regular expression
    ERR_DECIMAL_PRECISION_LOSS = 316, // decimal precision loss
    ERR_DATETIME_INVALID = 317,       // invalid datetime operation
    ERR_USER_ERROR = 318,             // user-defined error via error()
    
    // -------------------------------------------------------------------------
    // 4xx - I/O Errors
    // -------------------------------------------------------------------------
    ERR_IO_ERROR = 400,               // generic I/O error
    ERR_FILE_NOT_FOUND = 401,         // file does not exist
    ERR_FILE_ACCESS_DENIED = 402,     // permission denied
    ERR_FILE_READ_ERROR = 403,        // error reading file
    ERR_FILE_WRITE_ERROR = 404,       // error writing file
    ERR_NETWORK_ERROR = 405,          // network operation failed
    ERR_NETWORK_TIMEOUT = 406,        // network request timeout
    ERR_PARSE_ERROR = 407,            // error parsing input format
    ERR_FORMAT_ERROR = 408,           // error formatting output
    ERR_ENCODING_ERROR = 409,         // character encoding error
    ERR_INVALID_URL = 410,            // invalid URL format
    ERR_HTTP_ERROR = 411,             // HTTP request error
    
    // -------------------------------------------------------------------------
    // 5xx - Internal Errors
    // -------------------------------------------------------------------------
    ERR_INTERNAL_ERROR = 500,         // generic internal error
    ERR_NOT_IMPLEMENTED = 501,        // feature not yet implemented
    ERR_INVALID_STATE = 502,          // invalid internal state
    ERR_MEMORY_CORRUPTION = 503,      // detected memory corruption
    ERR_TYPE_SYSTEM_ERROR = 504,      // type system inconsistency
    ERR_POOL_EXHAUSTED = 505,         // memory pool exhausted
    
} LambdaErrorCode;

// ============================================================================
// Source Location
// ============================================================================

typedef struct SourceLocation {
    const char* file;       // source file path (may be NULL for REPL)
    uint32_t line;          // 1-based line number
    uint32_t column;        // 1-based column number
    uint32_t end_line;      // end line for multi-line spans
    uint32_t end_column;    // end column
    const char* source;     // pointer to source text (for context extraction)
} SourceLocation;

// ============================================================================
// Stack Frame
// ============================================================================

typedef struct StackFrame {
    const char* function_name;  // function name (or "<script>" for top-level)
    SourceLocation location;    // call site location
    struct StackFrame* next;    // next frame (toward main)
} StackFrame;

// ============================================================================
// Lambda Error Structure
// ============================================================================

typedef struct LambdaError {
    LambdaErrorCode code;       // error code (e.g., 201)
    char* message;              // human-readable message (owned)
    SourceLocation location;    // where the error occurred
    StackFrame* stack_trace;    // call stack (if enabled)
    char* help;                 // suggestion text (owned, optional)
    void* details;              // error-specific details (optional)
    struct LambdaError* cause;  // chained error (optional)
} LambdaError;

// ============================================================================
// Debug Info for Stack Trace Mapping (Native Stack Walking)
// ============================================================================

// Debug information for a compiled function
typedef struct FuncDebugInfo {
    void* native_addr_start;        // start of native code
    void* native_addr_end;          // end of native code (computed via address ordering)
    const char* lambda_func_name;   // Lambda function name
    const char* source_file;        // source file path
    uint32_t source_line;           // line number of function definition
} FuncDebugInfo;

// Build debug info table from MIR-compiled functions (call after MIR_link)
// Collects function addresses, sorts by address, and computes boundaries
// Returns opaque pointer to internal list (sorted by address)
void* build_debug_info_table(void* mir_ctx);

// Look up debug info for a native address using binary search
// Returns NULL if address is not in any Lambda function (runtime/system code)
FuncDebugInfo* lookup_debug_info(void* debug_info_list, void* addr);

// Free debug info table (call during cleanup)
void free_debug_info_table(void* debug_info_list);

// ============================================================================
// Error API
// ============================================================================

// Error code utilities
const char* err_code_name(LambdaErrorCode code);
const char* err_code_message(LambdaErrorCode code);
const char* err_category_name(LambdaErrorCode code);

// Error creation
LambdaError* err_create(LambdaErrorCode code, const char* message, SourceLocation* location);
LambdaError* err_createf(LambdaErrorCode code, SourceLocation* location, const char* format, ...);
LambdaError* err_create_simple(LambdaErrorCode code, const char* message);

// Error enrichment
void err_set_location(LambdaError* error, const char* file, uint32_t line, uint32_t col);
void err_add_help(LambdaError* error, const char* help);
void err_set_cause(LambdaError* error, LambdaError* cause);

// Stack trace
StackFrame* err_capture_stack_trace(void* debug_info_list, int max_frames);
void err_set_stack_trace(LambdaError* error, StackFrame* trace);

// Source context extraction
void err_extract_context(LambdaError* error, const char* source, int context_lines);
char* err_get_source_line(const char* source, uint32_t line_number);
int err_get_source_line_count(const char* source);

// Error output
char* err_format(LambdaError* error);
char* err_format_with_context(LambdaError* error, int context_lines);
char* err_format_json(LambdaError* error);
char* err_format_json_array(LambdaError** errors, int count);
void err_print(LambdaError* error);
void err_print_stack_trace(StackFrame* trace);

// Error cleanup
void err_free(LambdaError* error);
void err_free_stack_trace(StackFrame* trace);

// Source location helpers
SourceLocation src_loc(const char* file, uint32_t line, uint32_t col);
SourceLocation src_loc_span(const char* file, uint32_t line, uint32_t col, 
                            uint32_t end_line, uint32_t end_col);

#ifdef __cplusplus
}
#endif
