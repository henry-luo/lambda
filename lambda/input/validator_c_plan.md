# Lambda Schema Validator Implementation in C

## Revised Implementation Plan

Based on the existing Lambda transpiler implementation, this document outlines a plan to implement the schema validator in C by reusing existing data structures and functions.

## Overview

The Lambda Schema Validator will be implemented as a C library that integrates with the existing Lambda runtime, leveraging the transpiler's AST building, type system, and memory management infrastructure.

## Architecture

### Core Components Reuse

1. **Memory Management**: Reuse `VariableMemPool` from the transpiler
2. **Type System**: Extend existing `Type` structures and `TypeInfo` array
3. **AST Parsing**: Reuse Tree-sitter integration and AST building functions
4. **String Handling**: Leverage existing `String`, `StrView`, and `StrBuf` structures
5. **Container Types**: Utilize existing `List`, `Array`, `Map`, `Element` implementations

### New Components

```c
// Schema validation specific structures
typedef struct SchemaValidator SchemaValidator;
typedef struct ValidationResult ValidationResult;
typedef struct ValidationError ValidationError;
typedef struct ValidationContext ValidationContext;
```

## 1. Data Structure Extensions

### Enhanced Type System

```c
// Extend existing Type structure for validation
typedef struct TypeSchema {
    Type base;  // extends existing Type
    TypeId schema_type;  // SCHEMA_PRIMITIVE, SCHEMA_UNION, etc.
    void* schema_data;   // type-specific schema data
} TypeSchema;

// Schema-specific type IDs (extend EnumTypeId)
enum SchemaTypeId {
    LMD_SCHEMA_TYPE_START = LMD_TYPE_ERROR + 1,
    LMD_SCHEMA_PRIMITIVE,
    LMD_SCHEMA_UNION,
    LMD_SCHEMA_INTERSECTION,
    LMD_SCHEMA_ARRAY,
    LMD_SCHEMA_MAP,
    LMD_SCHEMA_ELEMENT,
    LMD_SCHEMA_FUNCTION,
    LMD_SCHEMA_REFERENCE,
    LMD_SCHEMA_OCCURRENCE,
    LMD_SCHEMA_LITERAL,
};
```

### Validation Structures

```c
// Validation error codes (similar to existing error handling)
typedef enum ValidationErrorCode {
    VALID_ERROR_TYPE_MISMATCH,
    VALID_ERROR_MISSING_FIELD,
    VALID_ERROR_UNEXPECTED_FIELD,
    VALID_ERROR_INVALID_ELEMENT,
    VALID_ERROR_CONSTRAINT_VIOLATION,
    VALID_ERROR_REFERENCE_ERROR,
    VALID_ERROR_OCCURRENCE_ERROR,
    VALID_ERROR_CIRCULAR_REFERENCE,
} ValidationErrorCode;

// Path segment for error reporting (reuses StrView)
typedef struct PathSegment {
    enum { PATH_FIELD, PATH_INDEX, PATH_ELEMENT, PATH_ATTRIBUTE } type;
    union {
        StrView field_name;
        long index;
        StrView element_tag;
        StrView attr_name;
    };
    struct PathSegment* next;
} PathSegment;

// Validation error (uses existing String for message)
typedef struct ValidationError {
    ValidationErrorCode code;
    String* message;
    PathSegment* path;
    TypeSchema* expected;
    Item actual;
    List* suggestions;  // List of String* suggestions
    struct ValidationError* next;
} ValidationError;

// Validation result (reuses existing List structure)
typedef struct ValidationResult {
    bool valid;
    List* errors;    // List of ValidationError*
    List* warnings;  // List of ValidationError*
} ValidationResult;

// Validation context (reuses existing structures)
typedef struct ValidationContext {
    VariableMemPool* pool;      // reuse transpiler memory pool
    PathSegment* path;          // current validation path
    HashMap* schema_types;      // reuse existing hashmap
    HashMap* visited;           // circular reference detection
    HashMap* custom_validators; // custom validation functions
    struct {
        bool strict;
        bool allow_unknown_fields;
        bool allow_empty_elements;
        int max_depth;
    } options;
} ValidationContext;
```

## 2. Schema Parser Integration

### Reuse Existing Parser Infrastructure

```c
// Extend existing Transpiler structure
typedef struct SchemaParser {
    Transpiler base;        // reuse transpiler infrastructure
    HashMap* type_registry; // registry of parsed type definitions
    List* type_definitions; // list of TypeDefinition structs
} SchemaParser;

// Type definition structure
typedef struct TypeDefinition {
    StrView name;
    TypeSchema* schema_type;
    TSNode source_node;     // original tree-sitter node
} TypeDefinition;

// Schema parsing functions (similar to existing build_* functions)
TypeSchema* parse_schema_from_source(const char* source, VariableMemPool* pool);
TypeDefinition* build_type_definition(SchemaParser* parser, TSNode type_node);
TypeSchema* build_schema_type(SchemaParser* parser, TSNode type_expr_node);
```

### Type Expression Building

```c
// Extend existing AST building functions
TypeSchema* build_primitive_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_union_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_array_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_map_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_element_schema(SchemaParser* parser, TSNode node);
TypeSchema* build_occurrence_schema(SchemaParser* parser, TSNode node);
```

## 3. Validation Engine Implementation

### Core Validation Functions

```c
// Main validation interface
typedef struct SchemaValidator {
    HashMap* schemas;           // loaded schemas by name
    VariableMemPool* pool;      // memory pool for validation
    ValidationContext* context; // validation context
    HashMap* custom_validators; // custom validation functions
} SchemaValidator;

// Core validation functions
SchemaValidator* schema_validator_create(VariableMemPool* pool);
ValidationResult* validate_item(SchemaValidator* validator, Item item, 
                               TypeSchema* schema, ValidationContext* context);
ValidationResult* validate_document(SchemaValidator* validator, Item document, 
                                   const char* schema_name);

// Type-specific validation functions (similar to existing pattern)
ValidationResult* validate_primitive(Item item, TypeSchema* schema, ValidationContext* ctx);
ValidationResult* validate_array(Item item, TypeSchema* schema, ValidationContext* ctx);
ValidationResult* validate_map(Item item, TypeSchema* schema, ValidationContext* ctx);
ValidationResult* validate_element(Item item, TypeSchema* schema, ValidationContext* ctx);
ValidationResult* validate_union(Item item, TypeSchema* schema, ValidationContext* ctx);
ValidationResult* validate_occurrence(Item item, TypeSchema* schema, ValidationContext* ctx);
```

### Error Reporting (Reuse Existing String System)

```c
// Error creation and formatting functions
ValidationError* create_validation_error(ValidationErrorCode code, 
                                        const char* message,
                                        PathSegment* path,
                                        VariableMemPool* pool);
String* format_validation_path(PathSegment* path, VariableMemPool* pool);
String* format_type_name(TypeSchema* type, VariableMemPool* pool);
List* suggest_similar_names(const char* name, List* available_names, VariableMemPool* pool);

// Error list management (reuse existing List functions)
void add_validation_error(ValidationResult* result, ValidationError* error);
void merge_validation_results(ValidationResult* dest, ValidationResult* src);
```

## 4. Integration with Existing Type System

### Type Information Extension

```c
// Extend existing TypeInfo structure
typedef struct SchemaTypeInfo {
    TypeInfo base;           // reuse existing TypeInfo
    bool (*validate_func)(Item item, TypeSchema* schema, ValidationContext* ctx);
    String* (*format_func)(TypeSchema* schema, VariableMemPool* pool);
} SchemaTypeInfo;

// Schema type registry (extends existing type_info array)
extern SchemaTypeInfo schema_type_info[];
```

### Runtime Integration

```c
// Extend existing Runtime structure
typedef struct ValidationRuntime {
    Runtime base;                    // reuse existing runtime
    HashMap* loaded_schemas;         // cache of loaded schemas
    SchemaValidator* default_validator;
} ValidationRuntime;

// Runtime functions
ValidationRuntime* validation_runtime_create(void);
int load_schema_file(ValidationRuntime* runtime, const char* schema_path);
SchemaValidator* get_validator(ValidationRuntime* runtime, const char* schema_name);
```

## 5. Doc Schema Specific Implementation

### Document Schema Validators

```c
// Custom validators for doc schema
typedef ValidationResult* (*CustomValidatorFunc)(Item item, TypeSchema* schema, 
                                                ValidationContext* context);

// Doc schema specific validators
ValidationResult* validate_citations(Item document, ValidationContext* context);
ValidationResult* validate_header_hierarchy(Item body, ValidationContext* context);
ValidationResult* validate_table_consistency(Item table, ValidationContext* context);
ValidationResult* validate_metadata_completeness(Item meta, ValidationContext* context);

// Custom validator registration
void register_doc_schema_validators(SchemaValidator* validator);
```

### Citation Validation Example

```c
ValidationResult* validate_citations(Item document, ValidationContext* context) {
    ValidationResult* result = create_validation_result(context->pool);
    
    // Extract document structure (reuse existing item access functions)
    if (get_type_id((LambdaItem){.item = document}) != LMD_TYPE_ELEMENT) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected document element", 
            context->path, context->pool));
        return result;
    }
    
    Element* doc_element = (Element*)document;
    
    // Find all citations (reuse existing element traversal)
    List* citations = find_elements_by_tag(doc_element, "citation", context->pool);
    
    // Get references from metadata
    Item meta_item = elmt_get(doc_element, s2it(create_string("meta", 4, context->pool)));
    List* references = extract_references(meta_item, context->pool);
    
    // Validate each citation
    for (long i = 0; i < citations->length; i++) {
        Item citation = list_get(citations, i);
        ValidationResult* cite_result = validate_single_citation(citation, references, context);
        merge_validation_results(result, cite_result);
    }
    
    return result;
}
```

## 6. Memory Management Integration

### Reuse Existing Pool System

```c
// Validation memory management (reuses existing patterns)
typedef struct ValidationMemory {
    VariableMemPool* ast_pool;    // reuse from transpiler
    VariableMemPool* temp_pool;   // temporary validation data
    Heap* runtime_heap;           // reuse runtime heap
} ValidationMemory;

// Memory functions (similar to existing alloc_* functions)
ValidationError* alloc_validation_error(ValidationContext* ctx, size_t extra_size);
PathSegment* alloc_path_segment(ValidationContext* ctx);
TypeSchema* alloc_schema_type(ValidationContext* ctx, TypeId schema_type, size_t size);
```

## 7. API Design

### Public Interface

```c
// Public API for validation
typedef struct LambdaValidator LambdaValidator;

// Validator lifecycle
LambdaValidator* lambda_validator_create(void);
int lambda_validator_load_schema(LambdaValidator* validator, const char* schema_file);
void lambda_validator_destroy(LambdaValidator* validator);

// Validation interface
typedef struct LambdaValidationResult {
    bool valid;
    char** errors;      // null-terminated array of error strings
    char** warnings;    // null-terminated array of warning strings
    int error_count;
    int warning_count;
} LambdaValidationResult;

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
    char** enabled_custom_rules;
    char** disabled_rules;
} LambdaValidationOptions;

void lambda_validator_set_options(LambdaValidator* validator, 
                                 LambdaValidationOptions* options);
```

## 8. Build Integration

### Makefile Integration

```makefile
# Add to existing Makefile
VALIDATOR_SOURCES = lambda/validator/validator.c \
                   lambda/validator/schema_parser.c \
                   lambda/validator/validation_engine.c \
                   lambda/validator/doc_validators.c \
                   lambda/validator/error_reporting.c

VALIDATOR_HEADERS = lambda/validator/validator.h

# Validation library target
libvalidator.a: $(VALIDATOR_SOURCES:.c=.o)
	ar rcs $@ $^

# Validation tool
lambda-validate: lambda/validator/main.c libvalidator.a lambda.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lvalidator -llambda $(LIBS)
```

### File Structure

```
lambda/
├── validator/
│   ├── validator.h          # Main public interface
│   ├── validator.c          # Core validation implementation
│   ├── schema_parser.c      # Schema parsing using existing AST
│   ├── validation_engine.c  # Type validation logic
│   ├── doc_validators.c     # Doc schema specific validators
│   ├── error_reporting.c    # Error formatting and reporting
│   ├── main.c              # CLI tool implementation
│   └── tests/
│       ├── test_validator.c
│       ├── test_schemas/
│       └── test_documents/
```

## 9. Implementation Phases

### Phase 1: Core Infrastructure (2-3 weeks)
- [ ] Extend existing Type system with schema types
- [ ] Implement basic ValidationContext and error structures
- [ ] Create schema parser using existing Tree-sitter integration
- [ ] Basic primitive type validation

### Phase 2: Container Types (2-3 weeks)
- [ ] Array validation using existing Array/List structures
- [ ] Map validation using existing Map infrastructure
- [ ] Element validation using existing Element structure
- [ ] Union and occurrence type support

### Phase 3: Advanced Features (2 weeks)
- [ ] Reference resolution and circular detection
- [ ] Custom validator framework
- [ ] Error reporting and suggestion system
- [ ] Performance optimization

### Phase 4: Doc Schema Integration (1-2 weeks)
- [ ] Doc schema specific validators
- [ ] Citation validation
- [ ] Header hierarchy checking
- [ ] Metadata validation

### Phase 5: Tooling and Testing (1-2 weeks)
- [ ] CLI validation tool
- [ ] Comprehensive test suite
- [ ] Documentation and examples
- [ ] Performance benchmarking

## 10. Benefits of C Implementation

### Performance
- **Native Speed**: Direct memory access and optimized data structures
- **Memory Efficiency**: Reuse existing memory pools and avoid garbage collection
- **Integration**: Seamless integration with existing Lambda runtime

### Code Reuse
- **Type System**: Leverage existing comprehensive type definitions
- **Memory Management**: Reuse battle-tested memory pool system
- **String Handling**: Utilize optimized string and view structures
- **Container Support**: Built on existing List, Array, Map implementations

### Maintainability
- **Consistent Style**: Follows existing codebase patterns and conventions
- **Shared Infrastructure**: Uses same build system, testing, and tooling
- **Unified Runtime**: Single runtime environment for parsing, validation, and execution

## 11. Future Extensions

### Language Bindings
- Python binding using existing C interface patterns
- JavaScript binding via WebAssembly compilation
- Rust binding using existing FFI approaches

### IDE Integration
- Language server protocol implementation
- VS Code extension using validation API
- Real-time validation feedback

### Advanced Features
- Schema evolution and compatibility checking
- Performance profiling and optimization
- Distributed validation for large documents
- Schema inference from example documents

This revised plan leverages the robust existing Lambda implementation while adding comprehensive schema validation capabilities that integrate seamlessly with the current architecture.
