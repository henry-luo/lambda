/**
 * @file schema_parser_new.cpp
 * @brief Lambda Schema Parser Implementation - AST Integration
 * @author Henry Luo
 * @license MIT
 * 
 * Simplified schema parser that uses existing Lambda AST infrastructure
 * instead of duplicating type building logic.
 */

#include "validator.hpp"
#include "../transpiler.hpp"
#include "../ast.hpp"
#include "../../lib/arraylist.h"
#include "../../lib/strview.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

// Debug flag - set to 0 to disable all SCHEMA_DEBUG output
#define ENABLE_SCHEMA_DEBUG 0

// ==================== Schema Parser Creation - Simplified ====================

SchemaParser* schema_parser_create(VariableMemPool* pool) {
    SchemaParser* parser = (SchemaParser*)pool_calloc(pool, sizeof(SchemaParser));
    
    // Initialize memory pool
    parser->pool = pool;
    
    // Initialize transpiler for AST building
    transpiler_init(&parser->base, pool);
    
    parser->type_registry = hashmap_new(sizeof(TypeSchema*), 0, 0, 0, NULL, NULL, NULL, NULL);
    parser->type_definitions = arraylist_new(16);
    
    return parser;
}

void schema_parser_destroy(SchemaParser* parser) {
    if (!parser) return;
    
    // Clean up hashmap
    if (parser->type_registry) {
        hashmap_destroy(parser->type_registry);
    }
    
    // Clean up arraylist
    if (parser->type_definitions) {
        arraylist_destroy(parser->type_definitions);
    }
    
    // Clean up transpiler
    transpiler_cleanup(&parser->base);
    
    // Note: memory pool cleanup handled by caller
}

// ==================== AST to Schema Conversion Functions ====================

/**
 * Convert an AST Type to a TypeSchema wrapper
 */
TypeSchema* ast_type_to_schema(Type* ast_type, VariableMemPool* pool) {
    if (!ast_type) return NULL;
    
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->base = *ast_type;  // Copy the base Type structure
    
    switch (ast_type->type_id) {
        case LMD_TYPE_MAP: {
            TypeMap* map_type = (TypeMap*)ast_type;
            schema->schema_type = LMD_SCHEMA_MAP;
            
            // Convert TypeMap to SchemaMap
            SchemaMap* schema_map = (SchemaMap*)pool_calloc(pool, sizeof(SchemaMap));
            schema_map->field_count = map_type->length;
            
            // Convert ShapeEntry list to SchemaMapField list
            SchemaMapField* prev_field = NULL;
            ShapeEntry* entry = map_type->shape;
            while (entry) {
                SchemaMapField* field = (SchemaMapField*)pool_calloc(pool, sizeof(SchemaMapField));
                field->name = *entry->name;  // Copy StrView
                field->type = ast_type_to_schema(entry->type, pool);
                field->required = true;  // Default to required
                
                if (!prev_field) {
                    schema_map->fields = field;
                } else {
                    prev_field->next = field;
                }
                prev_field = field;
                entry = entry->next;
            }
            
            schema->schema_data = schema_map;
            break;
        }
        
        case LMD_TYPE_ARRAY: {
            TypeArray* array_type = (TypeArray*)ast_type;
            schema->schema_type = LMD_SCHEMA_ARRAY;
            
            // Convert TypeArray to SchemaArray
            SchemaArray* schema_array = (SchemaArray*)pool_calloc(pool, sizeof(SchemaArray));
            schema_array->element_type = ast_type_to_schema(array_type->nested, pool);
            schema_array->occurrence = 0;  // Fixed size by default
            
            schema->schema_data = schema_array;
            break;
        }
        
        case LMD_TYPE_ELMT: {
            TypeElmt* elmt_type = (TypeElmt*)ast_type;
            schema->schema_type = LMD_SCHEMA_ELEMENT;
            
            // Convert TypeElmt to SchemaElement
            SchemaElement* schema_elmt = (SchemaElement*)pool_calloc(pool, sizeof(SchemaElement));
            schema_elmt->tag = elmt_type->name;
            
            // Convert element attributes (stored in shape like TypeMap)
            SchemaMapField* prev_attr = NULL;
            ShapeEntry* entry = elmt_type->shape;
            while (entry) {
                SchemaMapField* attr = (SchemaMapField*)pool_calloc(pool, sizeof(SchemaMapField));
                attr->name = *entry->name;
                attr->type = ast_type_to_schema(entry->type, pool);
                attr->required = true;
                
                if (!prev_attr) {
                    schema_elmt->attributes = attr;
                } else {
                    prev_attr->next = attr;
                }
                prev_attr = attr;
                entry = entry->next;
            }
            
            schema->schema_data = schema_elmt;
            break;
        }
        
        default: {
            // Primitive types
            schema->schema_type = LMD_SCHEMA_PRIMITIVE;
            
            SchemaPrimitive* primitive = (SchemaPrimitive*)pool_calloc(pool, sizeof(SchemaPrimitive));
            primitive->primitive_type = ast_type->type_id;
            
            schema->schema_data = primitive;
            break;
        }
    }
    
    return schema;
}

/**
 * Extract type definitions from AST nodes
 */
void extract_type_definitions_from_ast(SchemaParser* parser, AstNode* ast_node) {
    if (!ast_node) return;
    
    // For type assignment expressions, extract the type definition
    if (ast_node->node_type == AST_NODE_ASSIGN && 
        ast_node->type && ast_node->type->type_id == LMD_TYPE_TYPE) {
        
        AstAssignNode* assign_node = (AstAssignNode*)ast_node;
        if (assign_node->name) {
            TypeDefinition* def = (TypeDefinition*)pool_calloc(parser->pool, sizeof(TypeDefinition));
            
            // Convert pooled String* to StrView
            def->name.str = assign_node->name->chars;
            def->name.length = assign_node->name->len;
            
            // Extract the actual type from TypeType wrapper
            if (assign_node->value && assign_node->value->type && 
                assign_node->value->type->type_id == LMD_TYPE_TYPE) {
                TypeType* type_wrapper = (TypeType*)assign_node->value->type;
                def->schema_type = ast_type_to_schema(type_wrapper->type, parser->pool);
            } else {
                def->schema_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
            }
            
            def->is_exported = true;
            arraylist_append(parser->type_definitions, def);
            
            // Store in registry for quick lookup
            hashmap_set(parser->type_registry, &def->name, &def->schema_type);
        }
    }
    
    // Recursively process child nodes
    AstNode* child = ast_node->next;
    while (child) {
        extract_type_definitions_from_ast(parser, child);
        child = child->next;
    }
}

// ==================== Public Schema Parsing Functions ====================

TypeSchema* parse_schema_from_source(SchemaParser* parser, const char* source) {
    if (!parser || !source) return NULL;
    
    // Use transpiler to build AST
    AstNode* ast_root = transpiler_build_ast(&parser->base, source);
    if (!ast_root) {
        return NULL;
    }
    
    // Store source for reference
    parser->current_source = source;
    
    // Extract type definitions from AST
    extract_type_definitions_from_ast(parser, ast_root);
    
    // Look for a "Document" type definition first
    TypeSchema** document_schema = NULL;
    StrView document_name = {.str = "Document", .length = 8};
    if (hashmap_get(parser->type_registry, &document_name, &document_schema) && document_schema) {
        return *document_schema;
    }
    
    // If no Document type found, return the first type definition
    if (parser->type_definitions && parser->type_definitions->length > 0) {
        TypeDefinition* first_def = (TypeDefinition*)parser->type_definitions->data[0];
        if (first_def && first_def->schema_type) {
            return first_def->schema_type;
        }
    }
    
    // Fallback: convert the root AST type to schema
    if (ast_root && ast_root->type) {
        return ast_type_to_schema(ast_root->type, parser->pool);
    }
    
    return NULL;
}

TypeDefinition* build_type_definition(SchemaParser* parser, TSNode type_node) {
    // This function is deprecated in the new approach
    // Type definitions are extracted from AST directly
    return NULL;
}

TypeSchema* build_schema_type(SchemaParser* parser, TSNode type_expr_node) {
    if (!parser || ts_node_is_null(type_expr_node)) return NULL;
    
    // Build AST for this type expression
    AstNode* ast_node = build_expr(&parser->base, type_expr_node);
    if (!ast_node || !ast_node->type) {
        return create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
    
    // Convert AST type to schema
    return ast_type_to_schema(ast_node->type, parser->pool);
}

// ==================== Helper Functions ====================

void parse_all_type_definitions(SchemaParser* parser, TSNode root) {
    // Use transpiler to build complete AST
    const char* source = parser->current_source;
    if (!source) return;
    
    AstNode* ast_root = transpiler_build_ast(&parser->base, source);
    extract_type_definitions_from_ast(parser, ast_root);
}

void parse_all_type_definitions_recursive(SchemaParser* parser, TSNode node) {
    // This is now handled by extract_type_definitions_from_ast
}

TypeSchema* find_type_definition(SchemaParser* parser, const char* type_name) {
    if (!parser || !type_name) return NULL;
    
    StrView name = {.str = type_name, .length = strlen(type_name)};
    TypeSchema** schema = NULL;
    
    if (hashmap_get(parser->type_registry, &name, &schema) && schema) {
        return *schema;
    }
    
    return NULL;
}

// ==================== Schema Factory Functions (Unchanged) ====================

TypeSchema* create_primitive_schema(TypeId primitive_type, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_PRIMITIVE;
    
    SchemaPrimitive* primitive = (SchemaPrimitive*)pool_calloc(pool, sizeof(SchemaPrimitive));
    primitive->primitive_type = primitive_type;
    
    schema->schema_data = primitive;
    return schema;
}

TypeSchema* create_array_schema(TypeSchema* element_type, long min_len, long max_len, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_ARRAY;
    
    SchemaArray* array = (SchemaArray*)pool_calloc(pool, sizeof(SchemaArray));
    array->element_type = element_type;
    array->occurrence = 0;  // Fixed size
    
    schema->schema_data = array;
    return schema;
}

TypeSchema* create_union_schema(List* types, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_UNION;
    
    SchemaUnion* union_schema = (SchemaUnion*)pool_calloc(pool, sizeof(SchemaUnion));
    // TODO: Implement union schema creation from List
    
    schema->schema_data = union_schema;
    return schema;
}

TypeSchema* create_map_schema(TypeSchema* key_type, TypeSchema* value_type, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_MAP;
    
    SchemaMap* map = (SchemaMap*)pool_calloc(pool, sizeof(SchemaMap));
    map->is_open = true;  // Allow additional fields by default
    
    schema->schema_data = map;
    return schema;
}

TypeSchema* create_element_schema(const char* tag_name, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_ELEMENT;
    
    SchemaElement* element = (SchemaElement*)pool_calloc(pool, sizeof(SchemaElement));
    element->tag.str = tag_name;
    element->tag.length = strlen(tag_name);
    element->is_open = true;  // Allow additional attributes
    
    schema->schema_data = element;
    return schema;
}

TypeSchema* create_occurrence_schema(TypeSchema* base_type, long min_count, long max_count, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_OCCURRENCE;
    
    SchemaOccurrence* occurrence = (SchemaOccurrence*)pool_calloc(pool, sizeof(SchemaOccurrence));
    occurrence->base_type = base_type;
    if (max_count == 1 && min_count == 0) {
        occurrence->modifier = '?';
    } else if (min_count == 1 && max_count == -1) {
        occurrence->modifier = '+';
    } else if (min_count == 0 && max_count == -1) {
        occurrence->modifier = '*';
    }
    
    schema->schema_data = occurrence;
    return schema;
}

TypeSchema* create_reference_schema(const char* type_name, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_REFERENCE;
    
    schema->name.str = type_name;
    schema->name.length = strlen(type_name);
    
    return schema;
}

TypeSchema* create_literal_schema(Item literal_value, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->base.type_id = LMD_TYPE_TYPE;
    schema->schema_type = LMD_SCHEMA_LITERAL;
    
    SchemaLiteral* literal = (SchemaLiteral*)pool_calloc(pool, sizeof(SchemaLiteral));
    literal->literal_value = literal_value;
    
    schema->schema_data = literal;
    return schema;
}

// ==================== Utility Functions ====================

StrView strview_from_cstr(const char* str) {
    if (!str) return (StrView){NULL, 0};
    return (StrView){str, strlen(str)};
}

bool is_compatible_type(TypeId actual, TypeId expected) {
    if (actual == expected) return true;
    
    // Allow some type compatibility
    switch (expected) {
        case LMD_TYPE_NUMBER:
            return actual == LMD_TYPE_INT || actual == LMD_TYPE_FLOAT || actual == LMD_TYPE_DECIMAL;
        case LMD_TYPE_ANY:
            return true;
        default:
            return false;
    }
}

TypeSchema* resolve_reference(TypeSchema* ref_schema, HashMap* registry) {
    if (!ref_schema || ref_schema->schema_type != LMD_SCHEMA_REFERENCE || !registry) {
        return ref_schema;
    }
    
    TypeSchema** resolved = NULL;
    if (hashmap_get(registry, &ref_schema->name, &resolved) && resolved) {
        return *resolved;
    }
    
    return ref_schema;  // Return unresolved reference
}
