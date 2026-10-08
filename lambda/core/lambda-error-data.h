#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Shared error values are data; allocation and stack capture remain in runtime.
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
    ERR_UNHANDLED_ERROR = 228,        // error-returning call not handled with ^, handler, or or
    ERR_UNSUPPORTED_DYNAMIC_ABI = 229,// valid dynamic call exceeds physical dispatch ABI
    ERR_FUNCTION_ARGUMENT_LIMIT = 230,// Core Lambda function/call exceeds LAMBDA_MAX_FUNCTION_ARGS
    ERR_INVALIDATED_BINDING = 231,     // read after a hidden cross-frame mutation
    ERR_PLACE_COPY_MUTATED = 232,     // S9.3.1/CW24: writes through a copy of a
                                      // member/index read never reach its container
    ERR_BINDER_COLLISION = 233,       // `as T` conflicts with an in-scope name
    ERR_BINDER_BOUND_MISMATCH = 234,  // repeated binder sites disagree on bound
    ERR_BINDER_FORWARD_REF = 235,     // a binder name is used before its site
    ERR_BINDER_IN_RETURN = 236,       // binders are not valid in return contracts
    ERR_BINDER_TRAILING_THAT = 237,   // `that` follows an `as T` binder
    ERR_FILTER_BODY_NO_CURRENT = 238, // S10.1.6: a `|:` body must mention `~`

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
    ERR_CANCELLED = 319,              // cooperative task cancellation
    ERR_MAILBOX_FULL = 320,           // bounded task mailbox has no capacity
    
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
    bool is_native;             // true if this is a C/native function
    struct StackFrame* next;    // next frame (toward main)
} StackFrame;

// Native return addresses are captured without symbolization.  The debug-info
// table remains owned by the active script; materialization resolves these
// addresses only when an error consumer actually asks for a stack.
typedef struct RawStackTrace {
    void** return_addresses;
    int count;
    int capacity;
    void* debug_info;
    int max_frames;
} RawStackTrace;

enum { LAMBDA_ERROR_STACK_TRACE_DEFAULT_MAX_FRAMES = 64 };

// ============================================================================
// Lambda Error Structure
// ============================================================================

typedef struct LambdaError {
    // Keep the object prefix compatible with the runtime's Map/Container
    // prologue so an ERROR item can cross JS/Lambda boundaries without a
    // wrapper allocation or a second truth source for its identity.
    uint8_t type_id;
    uint8_t flags;
    uint8_t array_flags;
    uint8_t prologue_reserved;
    struct TypeMap* type;
    LambdaErrorCode code;       // error code (e.g., 201)
    bool is_heap;               // true when allocated on the Lambda GC heap
    bool is_static;             // true for pre-reserved fault storage; never free payload
    char* message;              // human-readable message (owned unless is_static)
    SourceLocation location;    // where the error occurred
    StackFrame* stack_trace;    // call stack (if enabled)
    RawStackTrace* raw_stack_trace; // unresolved native PCs, consumed on materialization
    char* help;                 // suggestion text (owned, optional)
    void* details;              // error-specific details (optional)
    struct LambdaError* cause;  // chained error (optional)
    uint64_t thrown_value_item; // original non-error JS throw payload, if any
    uint64_t js_name_item;      // JS-visible name for the merged Error lane
    uint64_t js_message_item;   // JS-visible message for the merged Error lane
    uint64_t js_cause_item;     // JS-visible cause, when supplied by JS
    uint64_t js_stack_item;     // lazily materialized JS-visible stack value
    // ordinary JS own properties live in a separate Map because this carrier
    // shares only the Container prologue, not Map's full shape/data layout.
    uint64_t js_properties_item;
    uint8_t js_class_id;        // JS built-in class identity for unified Error values
    uint8_t js_own_flags;       // own-property bits for Error's non-enumerable fields
} LambdaError;

