/**
 * @file validator.h
 * @brief New AST-Based Lambda Validator - Header
 * @author Henry Luo
 * @license MIT
 */

#pragma once

#include <ctime>
#include "../../lib/mempool.h"
#include "../../lib/hashmap.h"
#include "../transpiler.hpp"

// Forward declarations
typedef struct ValidationResult ValidationResult;
typedef struct ValidationError ValidationError;
typedef struct PathSegment PathSegment;
class SchemaValidator;  // C++ class, defined below

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
    VALID_ERROR_PATTERN_MISMATCH,  // String doesn't match pattern
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

// ==================== Schema Validator Class ====================

/**
 * SchemaValidator - Unified validator for Lambda schemas
 *
 * Validates data items against Lambda type schemas. Supports various input formats
 * (JSON, XML, HTML, etc.) and provides detailed error reporting with suggestions.
 *
 * Example usage:
 *   SchemaValidator* validator = SchemaValidator::create(pool);
 *   validator->load_schema(schema_source, "RootType");
 *   ValidationResult* result = validator->validate(item, "RootType");
 *   validator->destroy();
 */
class SchemaValidator {
private:
    // Core components
    Transpiler* transpiler;          // AST building and type extraction
    Pool* pool;                      // memory pool
    HashMap* type_definitions;       // registry of Type* definitions

    // Validation state
    PathSegment* current_path;       // current validation path
    int current_depth;               // current recursion depth
    int max_depth;                   // deprecated: use options.max_depth
    ValidationOptions options;       // validation configuration
    HashMap* visited_nodes;          // for circular reference detection
    clock_t validation_start_time;   // for timeout tracking

    // Private constructor - use create() factory method
    SchemaValidator(Pool* pool);

public:
    // Factory method and destructor
    static SchemaValidator* create(Pool* pool);
    void destroy();

    // Schema management
    int load_schema(const char* source, const char* root_type);

    // Validation methods
    ValidationResult* validate(ConstItem item, const char* type_name);
    ValidationResult* validate_with_format(ConstItem item, const char* type_name, const char* input_format);
    ValidationResult* validate_type(ConstItem item, Type* type);

    // Options management
    void set_options(ValidationOptions* options);
    ValidationOptions* get_options();
    static ValidationOptions default_options();
    void set_strict_mode(bool strict);
    void set_max_errors(int max);
    void set_timeout(int timeout_ms);
    void set_show_suggestions(bool show);
    void set_show_context(bool show);

    // Type lookup
    Type* find_type(const char* type_name);
    Type* resolve_type_reference(const char* type_name);

    // Accessors (for internal validation functions)
    Pool* get_pool() const { return pool; }
    Transpiler* get_transpiler() const { return transpiler; }
    PathSegment* get_current_path() const { return current_path; }
    void set_current_path(PathSegment* path) { current_path = path; }
    int get_current_depth() const { return current_depth; }
    void set_current_depth(int depth) { current_depth = depth; }
    HashMap* get_visited_nodes() const { return visited_nodes; }
    HashMap* get_type_definitions() const { return type_definitions; }
    ValidationOptions& get_options_ref() { return options; }
    clock_t get_validation_start_time() const { return validation_start_time; }
    void set_validation_start_time(clock_t time) { validation_start_time = time; }
};

// ==================== Core API (C Wrapper Functions for C Compatibility) ====================

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a new schema validator (C wrapper)
 */
SchemaValidator* schema_validator_create(Pool* pool);

/**
 * Destroy validator and cleanup resources (C wrapper)
 */
void schema_validator_destroy(SchemaValidator* validator);

/**
 * Load schema from Lambda source code (C wrapper)
 */
int schema_validator_load_schema(SchemaValidator* validator, const char* source, const char* root_type);

/**
 * Validate a typed item against a type name (C wrapper)
 */
ValidationResult* schema_validator_validate(SchemaValidator* validator, ConstItem item, const char* type_name);

/**
 * Validate a typed item against a type name with format-specific handling (C wrapper)
 */
ValidationResult* schema_validator_validate_with_format(
    SchemaValidator* validator,
    ConstItem item,
    const char* type_name,
    const char* input_format
);

/**
 * Validate a typed item against a specific Type* (C wrapper)
 */
ValidationResult* schema_validator_validate_type(SchemaValidator* validator, ConstItem item, Type* type);

// ==================== Validation Options (C Wrappers) ====================

/**
 * Set validation options (C wrapper)
 */
void schema_validator_set_options(SchemaValidator* validator, ValidationOptions* options);

/**
 * Get current validation options (C wrapper)
 */
ValidationOptions* schema_validator_get_options(SchemaValidator* validator);

/**
 * Create default validation options (C wrapper)
 */
ValidationOptions schema_validator_default_options();

/**
 * Convenience: Set strict mode (C wrapper)
 */
void schema_validator_set_strict_mode(SchemaValidator* validator, bool strict);

/**
 * Convenience: Set maximum error count (C wrapper)
 */
void schema_validator_set_max_errors(SchemaValidator* validator, int max);

/**
 * Convenience: Set validation timeout (C wrapper)
 */
void schema_validator_set_timeout(SchemaValidator* validator, int timeout_ms);

/**
 * Convenience: Enable error suggestions (C wrapper)
 */
void schema_validator_set_show_suggestions(SchemaValidator* validator, bool show);

/**
 * Convenience: Enable error context display (C wrapper)
 */
void schema_validator_set_show_context(SchemaValidator* validator, bool show);

// ==================== Type Extraction (C Wrappers) ====================

/**
 * Find type definition by name in validator registry (C wrapper)
 */
Type* schema_validator_find_type(SchemaValidator* validator, const char* type_name);

/**
 * Resolve a type reference with circular reference detection (C wrapper)
 */
Type* schema_validator_resolve_type_reference(SchemaValidator* validator, const char* type_name);

#ifdef __cplusplus
}
#endif

// ==================== Internal Validation Functions ====================
// These internal functions handle validation logic for specific type categories

/**
 * Extract Type* from AST node
 */
Type* extract_type_from_ast_node(AstNode* node);

// ==================== Internal Validation Functions ====================

/**
 * Main validation dispatcher
 */
ValidationResult* validate_against_type(SchemaValidator* validator, ConstItem item, Type* type);

/**
 * Validate against primitive type
 */
ValidationResult* validate_against_primitive_type(ConstItem item, Type* type);

/**
 * Validate against array type
 */
ValidationResult* validate_against_array_type(SchemaValidator* validator, ConstItem item, TypeArray* array_type);

/**
 * Validate against map type
 */
ValidationResult* validate_against_map_type(SchemaValidator* validator, ConstItem item, TypeMap* map_type);

/**
 * Validate against element type
 */
ValidationResult* validate_against_element_type(SchemaValidator* validator, ConstItem item, TypeElmt* element_type);

/**
 * Validate against union type (multiple valid types)
 */
ValidationResult* validate_against_union_type(SchemaValidator* validator, ConstItem item, Type** union_types, int type_count);

/**
 * Validate occurrence constraints (?, *, +, min/max)
 */
ValidationResult* validate_against_occurrence(SchemaValidator* validator, ConstItem* items, long item_count, Type* expected_type, Operator occurrence_op);

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
