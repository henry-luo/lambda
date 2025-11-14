/**
 * @file validator.h
 * @brief New AST-Based Lambda Validator - Header
 * @author Henry Luo
 * @license MIT
 */

#pragma once

#include <ctime>
#include "../lib/mempool.h"
#include "../lib/hashmap.h"
#include "transpiler.hpp"

// Forward declarations
typedef struct AstValidator AstValidator;
typedef struct ValidationResult ValidationResult;
typedef struct ValidationError ValidationError;
typedef struct PathSegment PathSegment;

// ==================== Validation Error System ====================

// Validation error codes
typedef enum ValidationErrorCode {
    VALID_ERROR_NONE = 0,
    VALID_ERROR_TYPE_MISMATCH,
    VALID_ERROR_MISSING_FIELD,
    VALID_ERROR_UNEXPECTED_FIELD,
    VALID_ERROR_NULL_VALUE,
    VALID_ERROR_INVALID_ELEMENT,
    VALID_ERROR_CONSTRAINT_VIOLATION,
    VALID_ERROR_REFERENCE_ERROR,
    VALID_ERROR_OCCURRENCE_ERROR,
    VALID_ERROR_CIRCULAR_REFERENCE,
    VALID_ERROR_PARSE_ERROR,
} ValidationErrorCode;

// Path segment types for error reporting
typedef enum PathSegmentType {
    PATH_FIELD,      // .field_name
    PATH_INDEX,      // [index]
    PATH_ELEMENT,    // <element_tag>
    PATH_ATTRIBUTE,  // @attr_name
    PATH_UNION,      // union alternative
} PathSegmentType;

// Path segment structure
typedef struct PathSegment {
    PathSegmentType type;
    union {
        StrView field_name;
        long index;
        StrView element_tag;
        StrView attr_name;
    } data;
    struct PathSegment* next;
} PathSegment;

// Validation error structure
typedef struct ValidationError {
    ValidationErrorCode code;
    String* message;           // Error message
    PathSegment* path;         // Path to error location
    void* expected;            // Expected type (optional)
    Item actual;               // Actual value (optional)
    List* suggestions;         // List of String* suggestions (optional)
    struct ValidationError* next;
} ValidationError;

// Validation warning (same as error but non-fatal)
typedef struct ValidationWarning {
    ValidationErrorCode code;
    String* message;           // Error message
    PathSegment* path;         // Path to error location
    void* expected;            // Expected type (optional)
    Item actual;               // Actual value (optional)
    List* suggestions;         // List of String* suggestions (optional)
    struct ValidationWarning* next;
} ValidationWarning;

// Validation result
typedef struct ValidationResult {
    bool valid;                // Overall validation result
    ValidationError* errors;   // Linked list of errors
    ValidationWarning* warnings; // Linked list of warnings
    int error_count;           // Number of errors
    int warning_count;         // Number of warnings
} ValidationResult;

// Validation options
typedef struct ValidationOptions {
    // strictness levels
    bool strict_mode;              // treat warnings as errors
    bool allow_unknown_fields;     // allow extra fields in maps
    bool allow_empty_elements;     // allow elements without content

    // limits
    int max_depth;                 // maximum validation depth (0 = unlimited)
    int timeout_ms;                // validation timeout in milliseconds (0 = no limit)
    int max_errors;                // stop after N errors (0 = unlimited)

    // error reporting
    bool show_suggestions;         // include suggestions in error messages
    bool show_context;             // show additional context in errors

    // custom rules
    char** enabled_rules;          // custom rules to enable
    char** disabled_rules;         // rules to disable
} ValidationOptions;

// Legacy typedef for backward compatibility - will be phased out
typedef ValidationResult AstValidationResult;
typedef ValidationError AstValidationError;
typedef ValidationOptions AstValidationOptions;
typedef ValidationErrorCode AstValidationErrorType;

// Legacy enum mappings
#define AST_VALID_ERROR_NONE VALID_ERROR_NONE
#define AST_VALID_ERROR_TYPE_MISMATCH VALID_ERROR_TYPE_MISMATCH
#define AST_VALID_ERROR_MISSING_FIELD VALID_ERROR_MISSING_FIELD
#define AST_VALID_ERROR_NULL_VALUE VALID_ERROR_NULL_VALUE
#define AST_VALID_ERROR_CONSTRAINT_VIOLATION VALID_ERROR_CONSTRAINT_VIOLATION
#define AST_VALID_ERROR_PARSE_ERROR VALID_ERROR_PARSE_ERROR
#define AST_VALID_ERROR_OCCURRENCE_ERROR VALID_ERROR_OCCURRENCE_ERROR
#define AST_VALID_ERROR_REFERENCE_ERROR VALID_ERROR_REFERENCE_ERROR

struct VisitedEntry {
    StrView key;
    bool visited;
};

// Validation context
typedef struct AstValidator {
    Transpiler* transpiler;          // direct use of transpiler for AST
    Pool* pool;                      // memory pool
    HashMap* type_definitions;       // registry of Type* definitions
    PathSegment* current_path;       // current validation path
    int current_depth;               // current recursion depth
    int max_depth;                   // deprecated: use options.max_depth
    ValidationOptions options;       // validation configuration
    HashMap* visited_nodes;          // for circular reference detection
    clock_t validation_start_time;   // for timeout tracking
} AstValidator;

// Schema validator structure
typedef struct SchemaValidator {
    HashMap* schemas;              // Loaded schemas by name
    Pool* pool;         // Memory pool
    void* context;                 // Default validation context
    void* custom_validators;       // Registered custom validators
    ValidationOptions default_options;  // Default validation options
} SchemaValidator;

// ==================== Core API ====================

/**
 * Create a new AST-based validator
 */
AstValidator* ast_validator_create(Pool* pool);

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
ValidationResult* ast_validator_validate(AstValidator* validator, ConstItem item, const char* type_name);

/**
 * Validate a typed item against a type name with format-specific handling
 * @param validator The validator instance
 * @param item The item to validate
 * @param type_name The name of the type to validate against
 * @param input_format Format hint ("xml", "html", "json", etc.) or NULL for auto-detect
 */
ValidationResult* ast_validator_validate_with_format(
    AstValidator* validator,
    ConstItem item,
    const char* type_name,
    const char* input_format
);

/**
 * Validate a typed item against a specific Type*
 */
ValidationResult* ast_validator_validate_type(AstValidator* validator, ConstItem item, Type* type);

// ==================== Validation Options ====================

/**
 * Set validation options
 */
void ast_validator_set_options(AstValidator* validator, ValidationOptions* options);

/**
 * Get current validation options
 */
ValidationOptions* ast_validator_get_options(AstValidator* validator);

/**
 * Create default validation options
 */
ValidationOptions ast_validator_default_options();

/**
 * Convenience: Set strict mode
 */
void ast_validator_set_strict_mode(AstValidator* validator, bool strict);

/**
 * Convenience: Set maximum error count
 */
void ast_validator_set_max_errors(AstValidator* validator, int max);

/**
 * Convenience: Set validation timeout
 */
void ast_validator_set_timeout(AstValidator* validator, int timeout_ms);

/**
 * Convenience: Enable error suggestions
 */
void ast_validator_set_show_suggestions(AstValidator* validator, bool show);

/**
 * Convenience: Enable error context display
 */
void ast_validator_set_show_context(AstValidator* validator, bool show);

// ==================== Type Extraction ====================

/**
 * Extract Type* from AST node
 */
Type* extract_type_from_ast_node(AstNode* node);

/**
 * Find type definition by name in validator registry
 */
Type* ast_validator_find_type(AstValidator* validator, const char* type_name);

/**
 * Resolve a type reference with circular reference detection
 * Returns the resolved Type* or nullptr if not found or circular
 */
Type* ast_validator_resolve_type_reference(AstValidator* validator, const char* type_name);

// ==================== Validation Functions ====================

/**
 * Main validation dispatcher
 */
ValidationResult* validate_against_type(AstValidator* validator, ConstItem item, Type* type);

/**
 * Validate against primitive type
 */
ValidationResult* validate_against_primitive_type(ConstItem item, Type* type);

/**
 * Validate against array type
 */
ValidationResult* validate_against_array_type(AstValidator* validator, ConstItem item, TypeArray* array_type);

/**
 * Validate against map type
 */
ValidationResult* validate_against_map_type(AstValidator* validator, ConstItem item, TypeMap* map_type);

/**
 * Validate against element type
 */
ValidationResult* validate_against_element_type(AstValidator* validator, ConstItem item, TypeElmt* element_type);

/**
 * Validate against union type (multiple valid types)
 */
ValidationResult* validate_against_union_type(AstValidator* validator, ConstItem item, Type** union_types, int type_count);

/**
 * Validate occurrence constraints (?, *, +, min/max)
 */
ValidationResult* validate_against_occurrence(AstValidator* validator, ConstItem* items, long item_count, Type* expected_type, Operator occurrence_op);

// ==================== Error Handling ====================

/**
 * Create validation result
 */
ValidationResult* create_validation_result(Pool* pool);

/**
 * Create validation error
 */
ValidationError* create_validation_error(ValidationErrorCode code, const char* message, PathSegment* path, Pool* pool);

/**
 * Create validation error with expected/actual information
 */
ValidationError* create_validation_error_ex(
    ValidationErrorCode code,
    const char* message,
    PathSegment* path,
    Type* expected_type,
    ConstItem actual_item,
    Pool* pool
);

/**
 * Add error to validation result
 */
void add_validation_error(ValidationResult* result, ValidationError* error);

/**
 * Add warning to validation result
 */
void add_validation_warning(ValidationResult* result, ValidationWarning* warning);

/**
 * Merge validation results
 */
void merge_validation_results(ValidationResult* dest, ValidationResult* src);

/**
 * Generate validation report
 */
String* generate_validation_report(ValidationResult* result, Pool* pool);

/**
 * Generate JSON validation report
 */
/**
 * Generate JSON validation report
 */
String* generate_json_report(ValidationResult* result, Pool* pool);

/**
 * Generate field name suggestions based on typo
 */
List* generate_field_suggestions(const char* typo_field, TypeMap* map_type, Pool* pool);

/**
 * Generate type mismatch suggestions
 */
List* generate_type_suggestions(TypeId actual_type, Type* expected_type, Pool* pool);

/**
 * Generate suggestions for a validation error
 */
List* generate_error_suggestions(ValidationError* error, Pool* pool);

/**
 * Format error with context
 */
String* format_error_with_context(ValidationError* error, Pool* pool);

/**
 * Format validation path
 */
String* format_validation_path(PathSegment* path, Pool* pool);

/**
 * Format type name
 */
String* format_type_name(void* type, Pool* pool);

/**
 * Free validation result
 */
void validation_result_destroy(ValidationResult* result);

// Legacy function names for backward compatibility
#define create_ast_validation_result create_validation_result
#define create_ast_validation_error create_validation_error
#define add_ast_validation_error add_validation_error
#define free_ast_validation_result validation_result_destroy

// ==================== Utility Functions ====================

/**
 * Unwrap XML document wrapper element
 * XML parsers often wrap content in a <document> root element
 */
ConstItem unwrap_xml_document(ConstItem item, Pool* pool);

/**
 * Unwrap HTML document wrapper element
 * Handle HTML-specific quirks and wrapper elements
 */
ConstItem unwrap_html_document(ConstItem item, Pool* pool);

/**
 * Detect input format from item structure
 * Returns format hint string or NULL if unknown
 */
const char* detect_input_format(ConstItem item);

/**
 * Check if ConstItem is compatible with Type*
 */
bool is_item_compatible_with_type(ConstItem item, Type* type);

/**
 * Get string representation of Type*
 */
const char* type_to_string(Type* type);

void print_validation_result(ValidationResult* result);
