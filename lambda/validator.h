/**
 * @file validator.h
 * @brief New AST-Based Lambda Validator - Header
 * @author Henry Luo
 * @license MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "../lib/mem-pool/include/mem_pool.h"
#include "../lib/hashmap.h"
#include "lambda-data.hpp"
#include "transpiler.hpp"

// Forward declarations
typedef struct AstValidator AstValidator;
typedef struct AstValidationContext AstValidationContext;
typedef struct AstValidationResult AstValidationResult;
typedef struct AstValidationError AstValidationError;

// Validation error types
typedef enum {
    AST_VALID_ERROR_NONE = 0,
    AST_VALID_ERROR_TYPE_MISMATCH,
    AST_VALID_ERROR_MISSING_FIELD,
    AST_VALID_ERROR_CONSTRAINT_VIOLATION,
    AST_VALID_ERROR_PARSE_ERROR,
    AST_VALID_ERROR_OCCURRENCE_ERROR
} AstValidationErrorType;

// Validation options
typedef struct {
    bool strict_mode;           // Strict type checking
    bool allow_unknown_fields;  // Allow fields not in schema
    bool allow_empty_elements;  // Allow empty elements
    int max_depth;             // Maximum validation depth
    int timeout_ms;            // Validation timeout
} AstValidationOptions;

// Validation error structure
typedef struct AstValidationError {
    AstValidationErrorType error_type;
    char* message;
    char* path;                // JSON path to error location
    Type* expected_type;       // Expected type from AST
    TypedItem actual_item;     // Actual item that failed
    struct AstValidationError* next;
} AstValidationError;

// Validation result structure
typedef struct AstValidationResult {
    bool valid;
    int error_count;
    AstValidationError* errors;
    VariableMemPool* pool;     // Memory pool for cleanup
} AstValidationResult;

// Validation context
typedef struct AstValidationContext {
    AstValidator* validator;
    char* current_path;
    int current_depth;
    AstValidationOptions options;
    VariableMemPool* pool;
} AstValidationContext;

// Main validator structure
typedef struct AstValidator {
    Transpiler* transpiler;           // Direct use of transpiler for AST
    VariableMemPool* pool;           // Memory pool
    HashMap* type_definitions;       // Registry of Type* definitions
    AstValidationOptions default_options;
} AstValidator;

// ==================== Core API ====================

/**
 * Create a new AST-based validator
 */
AstValidator* ast_validator_create(VariableMemPool* pool);

/**
 * Destroy validator and cleanup resources
 */
void ast_validator_destroy(AstValidator* validator);

/**
 * Load schema from Lambda source code
 */
int ast_validator_load_schema(AstValidator* validator, const char* source, const char* root_type);

/**
 * Validate a typed item against a type name
 */
AstValidationResult* ast_validator_validate(AstValidator* validator, TypedItem item, const char* type_name);

/**
 * Validate a typed item against a specific Type*
 */
AstValidationResult* ast_validator_validate_type(AstValidator* validator, TypedItem item, Type* type);

// ==================== Type Extraction ====================

/**
 * Extract Type* from AST node
 */
Type* extract_type_from_ast_node(AstNode* node);

/**
 * Find type definition by name in validator registry
 */
Type* ast_validator_find_type(AstValidator* validator, const char* type_name);

// ==================== Validation Functions ====================

/**
 * Main validation dispatcher
 */
AstValidationResult* validate_against_type(AstValidator* validator, TypedItem item, Type* type, AstValidationContext* ctx);

/**
 * Validate against primitive type
 */
AstValidationResult* validate_against_primitive_type(TypedItem item, Type* type, AstValidationContext* ctx);

/**
 * Validate against array type
 */
AstValidationResult* validate_against_array_type(AstValidator* validator, TypedItem item, TypeArray* array_type, AstValidationContext* ctx);

/**
 * Validate against map type
 */
AstValidationResult* validate_against_map_type(AstValidator* validator, TypedItem item, TypeMap* map_type, AstValidationContext* ctx);

/**
 * Validate against element type
 */
AstValidationResult* validate_against_element_type(AstValidator* validator, TypedItem item, TypeElmt* element_type, AstValidationContext* ctx);

/**
 * Validate against union type (multiple valid types)
 */
AstValidationResult* validate_against_union_type(AstValidator* validator, TypedItem item, Type** union_types, int type_count, AstValidationContext* ctx);

/**
 * Validate occurrence constraints (?, *, +, min/max)
 */
AstValidationResult* validate_occurrence_constraint(AstValidator* validator, TypedItem* items, long item_count, Type* expected_type, Operator occurrence_op, AstValidationContext* ctx);

// ==================== Error Handling ====================

/**
 * Create validation result
 */
AstValidationResult* create_ast_validation_result(VariableMemPool* pool);

/**
 * Create validation error
 */
AstValidationError* create_ast_validation_error(AstValidationErrorType type, const char* message, 
                                               const char* path, VariableMemPool* pool);

/**
 * Add error to validation result
 */
void add_ast_validation_error(AstValidationResult* result, AstValidationError* error);

/**
 * Free validation result
 */
void free_ast_validation_result(AstValidationResult* result);

// ==================== Utility Functions ====================

/**
 * Check if TypedItem is compatible with Type*
 */
bool is_item_compatible_with_type(TypedItem item, Type* type);

/**
 * Get string representation of Type*
 */
const char* type_to_string(Type* type);

#ifdef __cplusplus
}
#endif
