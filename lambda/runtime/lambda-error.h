/**
 * @file lambda-error.h
 * @brief Lambda Structured Error Handling System
 * 
 * Provides a comprehensive error code system with source location tracking
 * and stack trace support for better debugging and error reporting.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "../core/lambda-error-data.h"

// C14 system faults use non-allocating records until a recovery frame can
// deliver them to a pn boundary or execution handler. They are intentionally
// separate from ordinary typed ItemError construction.
typedef enum LambdaFaultReason {
    LAMBDA_FAULT_NONE = 0,
    LAMBDA_FAULT_STACK_OVERFLOW,
    LAMBDA_FAULT_SIDE_STACK_EXHAUSTION,
    LAMBDA_FAULT_OUT_OF_MEMORY,
    LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT,
    // D8.1.1v17: a REPL entry interrupted by SIGINT; the session rolls it back
    LAMBDA_FAULT_INTERRUPTED,
} LambdaFaultReason;

typedef struct LambdaFaultRecord {
    LambdaError error;
    LambdaFaultReason reason;
    LambdaErrorCode prior_error_code;
    bool active;
} LambdaFaultRecord;

// ============================================================================
// Debug Info for Stack Trace Mapping (Native Stack Walking)
// ============================================================================

// Debug information for a compiled function
typedef struct FuncDebugInfo {
    void* native_addr_start;        // start of native code
    void* native_addr_end;          // exclusive native-code end (next address or JIT allocation frontier)
    const char* lambda_func_name;   // Lambda function name
    const char* source_file;        // source file path
    uint32_t source_line;           // line number of function definition
} FuncDebugInfo;

// Build debug info table from MIR-compiled functions (call after MIR_link)
// Collects function addresses, sorts by address, and computes boundaries
// If func_name_map is provided, maps MIR internal names to Lambda user-friendly names
// Returns opaque pointer to internal list (sorted by address)
void* build_debug_info_table(void* mir_ctx, void* func_name_map);

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
typedef void* (*LambdaErrorHeapAllocFn)(size_t size, uint8_t type_id);
void err_set_heap_allocator(LambdaErrorHeapAllocFn alloc_fn);
LambdaError* err_create(LambdaErrorCode code, const char* message, SourceLocation* location);
LambdaError* err_create_heap(LambdaErrorCode code, const char* message, SourceLocation* location);
LambdaError* err_createfv(LambdaErrorCode code, SourceLocation* location,
                          const char* format, va_list args);
LambdaError* err_createf(LambdaErrorCode code, SourceLocation* location, const char* format, ...);
LambdaError* err_create_simple(LambdaErrorCode code, const char* message);

// Fault-record setup performs no allocation and may be called after an
// ordinary rich-error allocation has already failed.
void lambda_fault_record_init(LambdaFaultRecord* record);
void lambda_fault_record_prepare(LambdaFaultRecord* record,
                                 LambdaFaultReason reason,
                                 LambdaErrorCode prior_error_code);
void lambda_fault_record_from_error_allocation_failure(
    LambdaFaultRecord* record, LambdaErrorCode prior_error_code);
const char* lambda_fault_reason_name(LambdaFaultReason reason);
LambdaError* lambda_fault_record_error(LambdaFaultRecord* record);

// Error enrichment
void err_set_location(LambdaError* error, const char* file, uint32_t line, uint32_t col);
void err_add_help(LambdaError* error, const char* help);
void err_set_cause(LambdaError* error, LambdaError* cause);

// Stack trace
RawStackTrace* err_capture_raw_stack_trace(void* debug_info_list, int max_frames);
StackFrame* err_materialize_raw_stack_trace(RawStackTrace* raw_trace);
void err_free_raw_stack_trace(RawStackTrace* raw_trace);
void err_ensure_stack_trace(LambdaError* error);
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
// severity-labeled formatter/printer: --static-warning prints downgraded
// semantic errors as "warning[E…]" through the same context formatter.
char* err_format_with_context_labeled(LambdaError* error, int context_lines,
    const char* severity_label);
void err_print_warning(LambdaError* error);
void err_print_stack_trace(StackFrame* trace);

// Error cleanup
void err_free(LambdaError* error);
void err_free_stack_trace(StackFrame* trace);
void err_release_payload(LambdaError* error);

// GC hooks for heap-owned LambdaError objects
struct gc_heap;
void err_gc_trace(void* data, struct gc_heap* gc);
void err_gc_destroy(void* data);

// Source location helpers
SourceLocation src_loc(const char* file, uint32_t line, uint32_t col);
SourceLocation src_loc_span(const char* file, uint32_t line, uint32_t col, 
                            uint32_t end_line, uint32_t end_col);

// Set the current evaluation's error, capturing a native stack trace. It was a
// file-local helper in lambda-eval.cpp until Tier-3 CRUD (write_set.cpp) needed
// the same reporting; promoted rather than copied (one error path, one trace).
void set_runtime_error(LambdaErrorCode code, const char* format, ...)
    __attribute__((format(printf, 2, 3)));

#ifdef __cplusplus
}
#endif
