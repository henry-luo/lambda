#ifndef SCHEMA_AST_HPP
#define SCHEMA_AST_HPP

/**
 * @file schema_ast.hpp
 * @brief Unified Schema and AST Type System for Lambda
 * @author GitHub Copilot
 * @license MIT
 * 
 * This header provides the unified type system that bridges schema validation
 * and AST building in Lambda Script. It integrates the schema parser functionality
 * into the main transpiler pipeline.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "../lib/hashmap.h"
#include "../lib/arraylist.h"
#include "../lib/strview.h"
#include "../lib/mem-pool/include/mem_pool.h"

#ifdef __cplusplus
}
#endif

#include "lambda-data.hpp"
#include "ast.hpp"

// ==================== Unified Schema Type System ====================

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

// Enhanced type schema structure that bridges validation and runtime
typedef struct TypeSchema {
    Type base;              // Extends existing Type structure for runtime compatibility
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
    long min_count;           // Minimum occurrences
    long max_count;           // Maximum occurrences (-1 for unlimited)
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
    long min_count;           // Minimum occurrences
    long max_count;           // Maximum occurrences (-1 for unlimited)
} SchemaOccurrence;

typedef struct SchemaLiteral {
    Item literal_value;        // Specific literal value
} SchemaLiteral;

typedef struct SchemaReference {
    StrView type_name;         // Referenced type name
    TypeSchema* resolved_type; // Resolved type (NULL if unresolved)
} SchemaReference;

// ==================== Enhanced AST Nodes ====================

// Enhanced type node that bridges AST and schema
typedef struct AstSchemaTypeNode : AstTypeNode {
    TypeSchema* schema_type;     // Schema validation info
    Type* runtime_type;          // Runtime execution type (derived from schema)
    bool is_schema_definition;   // True for type definitions
    StrView type_name;           // For type references
} AstSchemaTypeNode;

// Occurrence type node for Type?, Type+, Type*
typedef struct AstOccurrenceTypeNode : AstSchemaTypeNode {
    AstNode* base_type;          // Base type expression
    char occurrence_modifier;    // '?', '+', or '*'
} AstOccurrenceTypeNode;

// Union type node for Type1 | Type2
typedef struct AstUnionTypeNode : AstSchemaTypeNode {
    AstNode* left_type;          // Left type in union
    AstNode* right_type;         // Right type in union
    StrView operator_str;        // "|" operator
} AstUnionTypeNode;

// Type reference node for TypeName
typedef struct AstReferenceTypeNode : AstSchemaTypeNode {
    StrView referenced_name;     // Name of referenced type
    TypeSchema* resolved_schema; // Resolved schema (NULL if unresolved)
} AstReferenceTypeNode;

// Type definition node for type Name = TypeExpr
typedef struct AstTypeDefinitionNode : AstSchemaTypeNode {
    StrView definition_name;     // Name being defined
    AstNode* type_expression;    // Type expression
} AstTypeDefinitionNode;

// ==================== Type Registry ====================

// Type definition entry in the registry  
typedef struct TypeDefinition {
    StrView name;                // Type name
    TypeSchema* schema_type;     // Schema representation
    Type* runtime_type;          // Runtime representation
    TSNode source_node;          // Source Tree-sitter node
    bool is_exported;            // Whether type is exported
} TypeDefinition;

// Type registry entry for hashmap storage
typedef struct TypeRegistryEntry {
    TypeDefinition* definition;  // The type definition
    StrView name_key;           // Key for hashmap lookup
} TypeRegistryEntry;

// Type registry for storing and resolving type definitions
typedef struct TypeRegistry {
    HashMap* type_map;           // Name -> TypeDefinition mapping
    ArrayList* type_list;        // Ordered list of type definitions
    VariableMemPool* pool;       // Memory pool for allocations
} TypeRegistry;

// ==================== Enhanced Transpiler ====================

// Schema-aware transpiler
typedef struct SchemaTranspiler : Transpiler {
    TypeRegistry* type_registry;     // Type definition registry
    bool schema_mode;                // Enable schema validation
    ArrayList* pending_references;   // Unresolved type references
} SchemaTranspiler;

// ==================== Unified Type Creation Functions ====================

// Create schema types
TypeSchema* unified_create_primitive_schema(TypeId primitive_type, VariableMemPool* pool);
TypeSchema* unified_create_union_schema(TypeSchema** types, int type_count, VariableMemPool* pool);
TypeSchema* unified_create_array_schema(TypeSchema* element_type, long min_count, long max_count, VariableMemPool* pool);
TypeSchema* unified_create_map_schema(TypeSchema* key_type, TypeSchema* value_type, VariableMemPool* pool);
TypeSchema* unified_create_element_schema(StrView tag, SchemaMapField* attributes, TypeSchema** content_types, int content_count, VariableMemPool* pool);
TypeSchema* unified_create_occurrence_schema(TypeSchema* base_type, char modifier, VariableMemPool* pool);
TypeSchema* unified_create_reference_schema(StrView type_name, VariableMemPool* pool);
TypeSchema* unified_create_literal_schema(Item literal_value, VariableMemPool* pool);

// Bridge functions: schema to runtime types
Type* schema_to_runtime_type(TypeSchema* schema, VariableMemPool* pool);
TypeSchema* runtime_to_schema_type(Type* runtime_type, VariableMemPool* pool);

// Type registry functions
TypeRegistry* type_registry_create(VariableMemPool* pool);
void type_registry_destroy(TypeRegistry* registry);
bool type_registry_add(TypeRegistry* registry, StrView name, TypeSchema* schema_type, Type* runtime_type);
TypeDefinition* type_registry_lookup(TypeRegistry* registry, StrView name);
TypeSchema* type_registry_resolve_reference(TypeRegistry* registry, StrView type_name);

// Enhanced transpiler functions
SchemaTranspiler* schema_transpiler_create(VariableMemPool* pool);
void schema_transpiler_destroy(SchemaTranspiler* transpiler);
void schema_transpiler_enable_validation(SchemaTranspiler* transpiler);
void schema_transpiler_add_type_definition(SchemaTranspiler* transpiler, StrView name, TypeSchema* schema);

// Occurrence modifier utility functions
void occurrence_to_counts(char modifier, long* min_count, long* max_count);
char counts_to_occurrence(long min_count, long max_count);
bool validate_occurrence_counts(long min_count, long max_count);

// Validation error codes (for future validation integration)
typedef enum SchemaValidationError {
    SCHEMA_VALID_OK = 0,
    SCHEMA_ERROR_TYPE_MISMATCH,
    SCHEMA_ERROR_MISSING_FIELD,
    SCHEMA_ERROR_UNEXPECTED_FIELD,
    SCHEMA_ERROR_OCCURRENCE_VIOLATION,
    SCHEMA_ERROR_REFERENCE_UNRESOLVED,
    SCHEMA_ERROR_CIRCULAR_REFERENCE,
} SchemaValidationError;

#endif // SCHEMA_AST_HPP
