/* Enhanced safety constants and macros for Lambda Script */
#ifndef LAMBDA_SAFETY_H
#define LAMBDA_SAFETY_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Memory safety limits
#define MAX_STRING_LENGTH (16 * 1024 * 1024)  // 16MB max string
#define MAX_ARRAY_LENGTH (1024 * 1024)        // 1M max elements
#define MAX_PATH_LENGTH 4096                   // Max file path length

// Parsing safety limits
#define MAX_PARSING_DEPTH 64                  // Max nesting depth
#define MAX_PARSING_OPERATIONS 1000000        // Max parsing operations
#define MAX_PARSING_TIME 30                   // Max parsing time in seconds

// Memory alignment
#define ALIGN_SIZE sizeof(max_align_t)

// Safe casting macros
#define SAFE_CAST(ptr, expected_type, target_type) \
    (((ptr) && ((ptr)->type_id == (expected_type))) ? (target_type*)(ptr) : NULL)

#define VALIDATE_POINTER(ptr, error_return) \
    do { \
        if (!(ptr)) { \
            log_error("Null pointer at %s:%d", __FILE__, __LINE__); \
            return (error_return); \
        } \
    } while(0)

#define VALIDATE_ARRAY_BOUNDS(array, index, error_return) \
    do { \
        if (!(array) || (index) < 0 || (index) >= (array)->length) { \
            log_warn("Array bounds violation: index %ld, length %ld at %s:%d", \
                    (long)(index), (array) ? (long)(array)->length : -1L, __FILE__, __LINE__); \
            return (error_return); \
        } \
    } while(0)

// Function declarations
String* create_string_safe(const char* buffer, size_t buffer_len);
Item array_get_safe(Array* array, long index);
bool is_valid_pool_pointer(VariableMemPool* pool, void* ptr);
MemPoolError pool_variable_free_safe(VariableMemPool* pool, void* ptr);
bool is_safe_file_path(const char* path);

// Parsing context
typedef struct ParseContext ParseContext;
ParseContext* parse_context_create(size_t max_depth);
bool parse_context_check_limits(ParseContext* ctx);
void parse_context_enter_depth(ParseContext* ctx);
void parse_context_exit_depth(ParseContext* ctx);
void parse_context_destroy(ParseContext* ctx);

#endif // LAMBDA_SAFETY_H
