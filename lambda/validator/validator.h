#pragma once

/**
 * @file validator.h
 * @brief Lambda Schema Validator - C Implementation
 * @author Henry Luo
 * @license MIT
 * 
 * Schema validation library for Lambda data structures.
 * Integrates with existing Lambda transpiler infrastructure.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "../transpiler.h"

// ==================== Schema Type System Extensions ====================

// Schema-specific type IDs (extend existing EnumTypeId)
enum SchemaTypeId {
    LMD_SCHEMA_TYPE_START = LMD_TYPE_ERROR + 1,
    LMD_SCHEMA_PRIMITIVE,     // Built-in types (int, string, etc.)
    LMD_SCHEMA_UNION,         // Type1 | Type2 
    LMD_SCHEMA_INTERSECTION,  // Type1 & Type2
    LMD_SCHEMA_ARRAY,         // [Type*] or [Type+] etc.
    LMD_SCHEMA_MAP,           // {field: Type, ...}
    LMD_SCHEMA_ELEMENT,       // <tag attr: Type, Content*>
    LMD_SCHEMA_FUNCTION,      // (param: Type) => ReturnType
    LMD_SCHEMA_REFERENCE,     // TypeName reference
    LMD_SCHEMA_OCCURRENCE,    // Type?, Type+, Type*
    LMD_SCHEMA_LITERAL,       // Specific literal value
};

// Schema type structure (extends existing Type)
typedef struct TypeSchema {
    Type base;              // Extends existing Type structure
    TypeId schema_type;     // Schema-specific type ID
    void* schema_data;      // Type-specific schema data
    StrView name;           // Type name (for references)
    bool is_open;           // Allows additional fields (maps/elements)
} TypeSchema;

// Schema data structures for different types
typedef struct SchemaPrimitive {
    TypeId primitive_type;  // LMD_TYPE_INT, LMD_TYPE_STRING, etc.
} SchemaPrimitive;

typedef struct SchemaUnion {
    TypeSchema** types;     // Array of type schemas
    int type_count;         // Number of types in union
} SchemaUnion;

typedef struct SchemaArray {
    TypeSchema* element_type;  // Type of array elements
    char occurrence;           // '?', '+', '*', or 0 for fixed
} SchemaArray;

typedef struct SchemaMapField {
    StrView name;              // Field name
    TypeSchema* type;          // Field type
    bool required;             // Whether field is required
    struct SchemaMapField* next;
} SchemaMapField;

typedef struct SchemaMap {
    SchemaMapField* fields;    // Linked list of fields
    int field_count;           // Number of fields
    bool is_open;              // Allows additional fields
} SchemaMap;

typedef struct SchemaElement {
    StrView tag;               // Element tag name
    SchemaMapField* attributes; // Element attributes
    TypeSchema** content_types; // Content type array  
    int content_count;         // Number of content types
    bool is_open;              // Allows additional attributes
} SchemaElement;

typedef struct SchemaOccurrence {
    TypeSchema* base_type;     // Base type
    char modifier;             // '?', '+', or '*'
} SchemaOccurrence;

typedef struct SchemaLiteral {
    Item literal_value;        // Specific literal value
} SchemaLiteral;

// ==================== Validation Error System ====================

// Validation error codes
typedef enum ValidationErrorCode {
    VALID_ERROR_NONE = 0,
    VALID_ERROR_TYPE_MISMATCH,
    VALID_ERROR_MISSING_FIELD,
    VALID_ERROR_UNEXPECTED_FIELD,
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
    TypeSchema* expected;      // Expected type (optional)
    Item actual;               // Actual value (optional)
    List* suggestions;         // List of String* suggestions (optional)
    struct ValidationError* next;
} ValidationError;

// Validation warning (same as error but non-fatal)
typedef ValidationError ValidationWarning;

// Validation result
typedef struct ValidationResult {
    bool valid;                // Overall validation result
    ValidationError* errors;   // Linked list of errors
    ValidationWarning* warnings; // Linked list of warnings
    int error_count;           // Number of errors
    int warning_count;         // Number of warnings
} ValidationResult;

// ==================== Validation Context ====================

// Validation options
typedef struct ValidationOptions {
    bool strict_mode;              // Treat warnings as errors
    bool allow_unknown_fields;     // Allow extra fields in maps
    bool allow_empty_elements;     // Allow elements without content
    int max_depth;                 // Maximum validation depth
    int timeout_ms;                // Validation timeout (0 = no limit)
    char** enabled_rules;          // Custom rules to enable
    char** disabled_rules;         // Rules to disable
} ValidationOptions;

// Forward declaration
typedef struct ValidationContext ValidationContext;

// Custom validator function type
typedef ValidationResult* (*CustomValidatorFunc)(Item item, TypeSchema* schema, 
                                                ValidationContext* context);

// Custom validator registration
typedef struct CustomValidator {
    StrView name;                  // Validator name
    StrView description;           // Description
    CustomValidatorFunc func;      // Validation function
    struct CustomValidator* next;
} CustomValidator;

// Validation context
typedef struct ValidationContext {
    VariableMemPool* pool;         // Memory pool for allocations
    PathSegment* path;             // Current validation path
    HashMap* schema_registry;      // Registry of schema types
    HashMap* visited;              // Circular reference detection
    CustomValidator* custom_validators; // Custom validators
    ValidationOptions options;     // Validation options
    int current_depth;             // Current validation depth
} ValidationContext;

// ==================== Schema Parser ====================

// Schema parser (extends Transpiler)
typedef struct SchemaParser {
    Transpiler base;               // Reuse transpiler infrastructure
    VariableMemPool* pool;         // Memory pool for allocations
    HashMap* type_registry;        // Registry of parsed types
    ArrayList* type_definitions;   // List of TypeDefinition*
    const char* current_source;    // Current source being parsed
    TSTree* current_tree;          // Current syntax tree
} SchemaParser;

// Type definition
typedef struct TypeDefinition {
    StrView name;                  // Type name
    TypeSchema* schema_type;       // Parsed schema type
    TSNode source_node;            // Original Tree-sitter node
    bool is_exported;              // Whether type is public
} TypeDefinition;

// Schema parsing functions
SchemaParser* schema_parser_create(VariableMemPool* pool);
void schema_parser_destroy(SchemaParser* parser);
TypeSchema* parse_schema_from_source(SchemaParser* parser, const char* source);
TypeDefinition* build_type_definition(SchemaParser* parser, TSNode type_node);
TypeSchema* build_schema_type(SchemaParser* parser, TSNode type_expr_node);

// Enhanced type building functions with full Tree-sitter symbol support
TypeSchema* build_primitive_schema(SchemaParser* parser, TSNode node, TypeId type_id);
TypeSchema* build_primitive_schema_from_symbol(SchemaParser* parser, TSNode node);
TypeSchema* build_union_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_array_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_map_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_element_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_occurrence_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_reference_schema(SchemaParser* parser, TSNode node);

// Additional enhanced type builders
TypeSchema* build_list_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_object_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_function_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_primary_type_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_list_type_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_array_type_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_map_type_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_element_type_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_function_type_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_binary_type_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_binary_expression_schema(SchemaParser* parser, TSNode node);

// ==================== Schema Creation Functions ====================

// Schema factory functions
TypeSchema* create_primitive_schema(TypeId primitive_type, VariableMemPool* pool);
TypeSchema* create_array_schema(TypeSchema* element_type, long min_len, long max_len, VariableMemPool* pool);
TypeSchema* create_union_schema(List* types, VariableMemPool* pool);
TypeSchema* create_map_schema(TypeSchema* key_type, TypeSchema* value_type, VariableMemPool* pool);
TypeSchema* create_element_schema(const char* tag_name, VariableMemPool* pool);
TypeSchema* create_occurrence_schema(TypeSchema* base_type, long min_count, long max_count, VariableMemPool* pool);
TypeSchema* create_reference_schema(const char* type_name, VariableMemPool* pool);
TypeSchema* create_literal_schema(Item literal_value, VariableMemPool* pool);

// Utility functions
StrView strview_from_cstr(const char* str);
String* string_from_strview(StrView view, VariableMemPool* pool);
bool is_compatible_type(TypeId actual, TypeId expected);
TypeSchema* resolve_reference(TypeSchema* ref_schema, HashMap* registry);

// ==================== Validation Engine ====================

// Main validator structure
typedef struct SchemaValidator {
    HashMap* schemas;              // Loaded schemas by name
    VariableMemPool* pool;         // Memory pool
    ValidationContext* context;    // Default validation context
    CustomValidator* custom_validators; // Registered custom validators
    ValidationOptions default_options;  // Default validation options
} SchemaValidator;

// Validator lifecycle
SchemaValidator* schema_validator_create(VariableMemPool* pool);
void schema_validator_destroy(SchemaValidator* validator);
int schema_validator_load_schema(SchemaValidator* validator, const char* schema_source, 
                                const char* schema_name);
int schema_validator_load_schema_file(SchemaValidator* validator, const char* schema_path);

// Validation functions
ValidationResult* validate_item(SchemaValidator* validator, Item item, 
                               TypeSchema* schema, ValidationContext* context);
ValidationResult* validate_document(SchemaValidator* validator, Item document, 
                                   const char* schema_name);
ValidationResult* validate_with_options(SchemaValidator* validator, Item item,
                                       const char* schema_name, 
                                       ValidationOptions* options);

// Type-specific validation functions
ValidationResult* validate_primitive(Item item, TypeSchema* schema, ValidationContext* ctx);
ValidationResult* validate_union(Item item, TypeSchema* schema, ValidationContext* ctx);
ValidationResult* validate_array(Item item, TypeSchema* schema, ValidationContext* ctx);
ValidationResult* validate_map(Item item, TypeSchema* schema, ValidationContext* ctx);
ValidationResult* validate_element(Item item, TypeSchema* schema, ValidationContext* ctx);
ValidationResult* validate_occurrence(Item item, TypeSchema* schema, ValidationContext* ctx);
ValidationResult* validate_reference(Item item, TypeSchema* schema, ValidationContext* ctx);
ValidationResult* validate_literal(Item item, TypeSchema* schema, ValidationContext* ctx);

// Custom validator registration
void register_custom_validator(SchemaValidator* validator, const char* name,
                              const char* description, CustomValidatorFunc func);
void unregister_custom_validator(SchemaValidator* validator, const char* name);

// ==================== Error Reporting ====================

// Validation result management
ValidationResult* create_validation_result(VariableMemPool* pool);
void validation_result_destroy(ValidationResult* result);
void add_validation_error(ValidationResult* result, ValidationError* error);
void add_validation_warning(ValidationResult* result, ValidationWarning* warning);
void merge_validation_results(ValidationResult* dest, ValidationResult* src);

// Error creation and formatting
ValidationError* create_validation_error(ValidationErrorCode code, const char* message,
                                        PathSegment* path, VariableMemPool* pool);
PathSegment* create_path_segment(PathSegmentType type, VariableMemPool* pool);
String* format_validation_path(PathSegment* path, VariableMemPool* pool);
String* format_type_name(TypeSchema* type, VariableMemPool* pool);
String* format_validation_error(ValidationError* error, VariableMemPool* pool);

// Path manipulation
PathSegment* path_push_field(PathSegment* path, const char* field_name, VariableMemPool* pool);
PathSegment* path_push_index(PathSegment* path, long index, VariableMemPool* pool);
PathSegment* path_push_element(PathSegment* path, const char* tag, VariableMemPool* pool);
PathSegment* path_push_attribute(PathSegment* path, const char* attr_name, VariableMemPool* pool);
PathSegment* path_pop(PathSegment* path);

// Suggestion system
List* suggest_similar_names(const char* name, List* available_names, VariableMemPool* pool);
List* suggest_corrections(ValidationError* error, VariableMemPool* pool);

// ==================== Doc Schema Validators ====================

// Doc schema specific validators
ValidationResult* validate_citations(Item document, ValidationContext* context);
ValidationResult* validate_header_hierarchy(Item body, ValidationContext* context);
ValidationResult* validate_table_consistency(Item table, ValidationContext* context);
ValidationResult* validate_metadata_completeness(Item meta, ValidationContext* context);
ValidationResult* validate_cross_references(Item document, ValidationContext* context);

// Doc schema validator registration
void register_doc_schema_validators(SchemaValidator* validator);

// Citation validation helpers
List* find_citations_in_element(Element* element, VariableMemPool* pool);
List* extract_references_from_meta(Item meta, VariableMemPool* pool);
ValidationResult* validate_single_citation(Item citation, List* references, 
                                          ValidationContext* context);

// Header hierarchy helpers
typedef struct HeaderInfo {
    int level;           // Header level (1-6)
    StrView text;        // Header text
    PathSegment* path;   // Path to header
} HeaderInfo;

List* extract_headers(Element* body, VariableMemPool* pool);
ValidationResult* check_header_sequence(List* headers, ValidationContext* context);

// ==================== Public API ====================

// High-level public interface for embedding
typedef struct LambdaValidator LambdaValidator;

// Validator lifecycle
LambdaValidator* lambda_validator_create(void);
void lambda_validator_destroy(LambdaValidator* validator);
int lambda_validator_load_schema_file(LambdaValidator* validator, const char* schema_file);
int lambda_validator_load_schema_string(LambdaValidator* validator, const char* schema_source,
                                       const char* schema_name);

// Simple validation result for public API
typedef struct LambdaValidationResult {
    bool valid;
    char** errors;         // Null-terminated array of error strings
    char** warnings;       // Null-terminated array of warning strings
    int error_count;
    int warning_count;
} LambdaValidationResult;

// Validation interface
LambdaValidationResult* lambda_validate_file(LambdaValidator* validator, 
                                           const char* document_file,
                                           const char* schema_name);
LambdaValidationResult* lambda_validate_string(LambdaValidator* validator,
                                             const char* document_source,
                                             const char* schema_name);
void lambda_validation_result_free(LambdaValidationResult* result);

// Configuration
typedef struct LambdaValidationOptions {
    bool strict_mode;
    bool allow_unknown_fields;
    bool allow_empty_elements;
    int max_validation_depth;
    char** enabled_custom_rules;  // Null-terminated array
    char** disabled_rules;        // Null-terminated array
} LambdaValidationOptions;

void lambda_validator_set_options(LambdaValidator* validator, 
                                 LambdaValidationOptions* options);
LambdaValidationOptions* lambda_validator_get_options(LambdaValidator* validator);

// ==================== Utility Functions ====================

// Type system utilities
bool is_compatible_type(TypeId actual, TypeId expected);
bool is_optional_schema(TypeSchema* schema);
TypeSchema* resolve_reference(TypeSchema* ref_schema, HashMap* registry);
bool has_circular_reference(TypeSchema* schema, HashMap* visited);

// Item type checking (extends existing functions)
bool item_matches_primitive(Item item, TypeId expected_type);
bool item_is_container(Item item);
bool item_is_element_with_tag(Item item, const char* tag);

// String utilities (reuse existing string functions)
StrView strview_from_cstr(const char* str);
String* string_from_strview(StrView view, VariableMemPool* pool);
bool strview_equals(StrView a, StrView b);
int strview_compare(StrView a, StrView b);

// Memory management helpers
void* validator_alloc(ValidationContext* context, size_t size);
void* validator_calloc(ValidationContext* context, size_t size);
TypeSchema* alloc_schema_type(ValidationContext* context, TypeId schema_type, size_t size);

// Debug and testing functions
#ifdef DEBUG
void print_schema_type(TypeSchema* schema, int indent);
void print_validation_result(ValidationResult* result);
void print_validation_path(PathSegment* path);
#endif

#ifdef __cplusplus
}
#endif
