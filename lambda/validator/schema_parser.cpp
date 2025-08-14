/**
 * @file schema_parser.c
 * @brief Lambda Schema Parser Implementation - Tree-sitter Integration
 * @author Henry Luo
 * @license MIT
 */

#include "validator.h"
#include "../ts-enum.h"  // Include Tree-sitter symbols and field IDs
#include "../ast.h"      // Include field constants
#include "../../lib/arraylist.h"
#include "../../lib/strview.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

// External function declarations
extern "C" {
    TSParser* lambda_parser(void);
    TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
}

// Helper macro for node source access
#define schema_node_source(parser, node) {.str = (parser)->current_source + ts_node_start_byte(node), \
     .length = ts_node_end_byte(node) - ts_node_start_byte(node) }

// Helper function for node source access
StrView get_node_source(SchemaParser* parser, TSNode node) {
    return (StrView){
        .str = parser->current_source + ts_node_start_byte(node),
        .length = ts_node_end_byte(node) - ts_node_start_byte(node)
    };
}

// ==================== Schema Parser Creation ====================

SchemaParser* schema_parser_create(VariableMemPool* pool) {
    SchemaParser* parser = (SchemaParser*)pool_calloc(pool, sizeof(SchemaParser));
    
    // Initialize memory pool
    parser->pool = pool;
    
    // Initialize base transpiler components (only the ones we can access)
    parser->base.parser = lambda_parser();  // Use the same Tree-sitter parser
    
    parser->type_registry = hashmap_new(sizeof(TypeSchema*), 0, 0, 0, NULL, NULL, NULL, NULL);
    parser->type_definitions = arraylist_new(16);
    
    return parser;
}

void schema_parser_destroy(SchemaParser* parser) {
    if (!parser) return;
    
    if (parser->type_registry) {
        hashmap_free(parser->type_registry);
    }
    
    if (parser->base.parser) {
        ts_parser_delete(parser->base.parser);
    }
    
    if (parser->current_tree) {
        ts_tree_delete(parser->current_tree);
    }
    
    // Note: memory pool cleanup handled by caller
}

// ==================== Schema Parsing Functions ====================

TypeSchema* parse_schema_from_source(SchemaParser* parser, const char* source) {
    if (!parser || !source) return NULL;
    
    // Parse source using Tree-sitter
    TSTree* tree = lambda_parse_source(parser->base.parser, source);
    if (!tree) {
        return NULL;
    }
    
    // Store source and tree for later use
    parser->current_source = source;
    parser->current_tree = tree;
    
    TSNode root = ts_tree_root_node(tree);
    
    // First, collect all type definitions from the source
    parse_all_type_definitions(parser, root);
    
    // For now, try to find a "Document" type definition, or return the first one
    TypeSchema* document_schema = find_type_definition(parser, "Document");
    if (document_schema) {
        return document_schema;
    }
    
    // If no Document type found, try to build schema from first available type
    if (parser->type_definitions && parser->type_definitions->length > 0) {
        TypeDefinition* first_def = (TypeDefinition*)parser->type_definitions->data[0];
        if (first_def && first_def->schema_type) {
            return first_def->schema_type;
        }
    }
    
    // Fallback: try to build schema directly from root (old behavior)
    TypeSchema* schema = build_schema_type(parser, root);
    
    return schema;
}

TypeDefinition* build_type_definition(SchemaParser* parser, TSNode type_node) {
    if (!parser) return NULL;
    
    TypeDefinition* def = (TypeDefinition*)pool_calloc(parser->pool, sizeof(TypeDefinition));
    def->source_node = type_node;
    
    // For type_stam nodes, we need to get the assign_expr child
    TSNode assign_expr_node = {0};
    uint32_t child_count = ts_node_child_count(type_node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(type_node, i);
        if (ts_node_symbol(child) == sym_assign_expr) {
            assign_expr_node = child;
            break;
        }
    }
    
    if (!ts_node_is_null(assign_expr_node)) {
        // Get the identifier (first child of assign_expr)
        TSNode name_node = ts_node_child(assign_expr_node, 0);
        if (!ts_node_is_null(name_node)) {
            def->name = get_node_source(parser, name_node);
        }
        
        // Get the type expression (third child of assign_expr, after identifier and '=')
        TSNode type_expr_node = ts_node_child(assign_expr_node, 2);
        if (!ts_node_is_null(type_expr_node)) {
            def->schema_type = build_schema_type(parser, type_expr_node);
        } else {
            def->schema_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
        }
    } else {
        def->name = strview_from_cstr("UnnamedType");
        def->schema_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
    
    def->is_exported = true;
    
    return def;
}

TypeSchema* build_schema_type(SchemaParser* parser, TSNode type_expr_node) {
    if (!parser || ts_node_is_null(type_expr_node)) return NULL;
    
    TSSymbol symbol = ts_node_symbol(type_expr_node);
    const char* node_type = ts_node_type(type_expr_node);
    
    printf("[SCHEMA_DEBUG] build_schema_type: symbol=%d, node_type='%s'\n", symbol, node_type);
    printf("[SCHEMA_DEBUG] build_schema_type: sym_base_type=%d, sym_primary_type=%d, sym_identifier=%d\n", 
           sym_base_type, sym_primary_type, sym_identifier);
    
    // Handle different node types based on Tree-sitter symbols from ts-enum.h
    switch (symbol) {
        // Base type nodes
        case anon_sym_int:
        case sym_integer:
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_INT);
            
        case anon_sym_float:
        case sym_float:
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_FLOAT);
            
        case anon_sym_number:
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_FLOAT);
            
        case anon_sym_string:
        case sym_string:
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_STRING);
            
        case anon_sym_bool:
        case sym_true:
        case sym_false:
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_BOOL);
            
        case anon_sym_char:
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_STRING);  // Char is represented as string
            
        case anon_sym_symbol:
        case sym_symbol:
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_SYMBOL);
            
        case anon_sym_datetime:
        case sym_datetime:
        case anon_sym_date:
        case anon_sym_time:
        case sym_time:
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_DTIME);
            
        case anon_sym_decimal:
        case sym_decimal:
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_DECIMAL);
            
        case anon_sym_binary:
        case sym_binary:
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_BINARY);
            
        case anon_sym_null:
        case sym_null:
            printf("[SCHEMA_DEBUG] get_type_id: null case triggered\n");
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_NULL);
            
        // Complex type nodes
        case anon_sym_list:
        case sym_list:
            return build_list_schema(parser, type_expr_node);
            
        case anon_sym_array:
        case sym_array:
            return build_array_schema(parser, type_expr_node);
            
        case anon_sym_map:
        case sym_map:
            return build_map_schema(parser, type_expr_node);
            
        case anon_sym_element:
        case sym_element:
            return build_element_schema(parser, type_expr_node);
            
        case anon_sym_object:
            return build_object_schema(parser, type_expr_node);
            
        case anon_sym_function:
            return build_function_schema(parser, type_expr_node);
            
        // Type expressions
        case sym_base_type:
        case sym_primary_type:
            printf("[SCHEMA_DEBUG] build_schema_type: sym_base_type/sym_primary_type case, calling build_primary_type_schema\n");
            return build_primary_type_schema(parser, type_expr_node);
            
        case sym_list_type:
            return build_list_type_schema(parser, type_expr_node);
            
        case sym_array_type:
            return build_array_type_schema(parser, type_expr_node);
            
        case sym_map_type:
            return build_map_type_schema(parser, type_expr_node);
            
        case sym_element_type:
            return build_element_type_schema(parser, type_expr_node);
            
        case sym_fn_type:
            return build_function_type_schema(parser, type_expr_node);
            
        case sym_binary_type:
            return build_binary_type_schema(parser, type_expr_node);
            
        case sym_type_occurrence:
            return build_occurrence_schema(parser, type_expr_node);
            
        // Identifiers and references
        case sym_identifier:
            printf("[SCHEMA_DEBUG] build_schema_type: sym_identifier case, calling build_reference_schema\n");
            return build_reference_schema(parser, type_expr_node);
            
        // Binary expressions (for union types: Type1 | Type2)
        case sym_binary_expr:
            return build_binary_expression_schema(parser, type_expr_node);
            
        default:
            // Handle binary expressions for union types manually if not caught above
            if (ts_node_child_count(type_expr_node) >= 3) {
                TSNode op_node = ts_node_child(type_expr_node, 1);
                StrView op = get_node_source(parser, op_node);
                if (strview_equal(&op, "|")) {
                    return build_union_schema(parser, type_expr_node);
                }
            }
            
            // Add debug for unhandled node types
            const char* type_name = ts_node_type(type_expr_node);
            TSSymbol type_symbol = ts_node_symbol(type_expr_node);
            
            fprintf(stderr, "[SCHEMA_PARSER] WARNING: Unhandled node type in build_type_schema_for_node:\n");
            fprintf(stderr, "[SCHEMA_PARSER]   Node type: %s (symbol: %d)\n", type_name, type_symbol);
            fprintf(stderr, "[SCHEMA_PARSER]   Node text: '");
            // Print node text directly  
            uint32_t start_byte = ts_node_start_byte(type_expr_node);
            uint32_t end_byte = ts_node_end_byte(type_expr_node);
            uint32_t length = end_byte - start_byte;
            if (parser->current_source && length > 0) {
                fprintf(stderr, "%.*s", (int)length, parser->current_source + start_byte);
            }
            fprintf(stderr, "'\n");
            fprintf(stderr, "[SCHEMA_PARSER]   Defaulting to LMD_TYPE_ANY.\n");
            
            // Default to any type
            return create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
}

// ==================== Type Building Functions ====================

TypeSchema* build_primitive_schema(SchemaParser* parser, TSNode node, TypeId type_id) {
    if (!parser) return NULL;
    
    printf("[SCHEMA_DEBUG] build_primitive_schema: type_id=%d\n", type_id);
    return create_primitive_schema(type_id, parser->pool);
}

// Fallback for old-style primitive schema building
TypeSchema* build_primitive_schema_from_symbol(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    TSSymbol symbol = ts_node_symbol(node);
    TypeId primitive_type = LMD_TYPE_ANY;
    
    // This function still uses the old SYM_ constants for backward compatibility
    // TODO: Update these to use actual ts-enum.h symbols
    switch (symbol) {
        case sym_integer:
            primitive_type = LMD_TYPE_INT;
            break;
        case sym_float:
            primitive_type = LMD_TYPE_FLOAT;
            break;
        case sym_string:
            primitive_type = LMD_TYPE_STRING;
            break;
        case sym_null:
            primitive_type = LMD_TYPE_NULL;
            break;
        case sym_true:
        case sym_false:
            primitive_type = LMD_TYPE_BOOL;
            break;
        default:
            primitive_type = LMD_TYPE_ANY;
            break;
    }
    
    return create_primitive_schema(primitive_type, parser->pool);
}

TypeSchema* build_union_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    ArrayList* types = arraylist_new(2);
    
    // Extract left and right types from binary expression
    TSNode left_node = ts_node_child(node, 0);
    TSNode right_node = ts_node_child(node, 2);
    
    TypeSchema* left_type = build_schema_type(parser, left_node);
    TypeSchema* right_type = build_schema_type(parser, right_node);
    
    if (left_type) arraylist_append(types, left_type);
    if (right_type) arraylist_append(types, right_type);
    
    // Convert ArrayList to List for create_union_schema
    List* type_list = (List*)pool_calloc(parser->pool, sizeof(List));
    type_list->length = types->length;
    type_list->capacity = types->length;
    type_list->items = (Item*)pool_calloc(parser->pool, sizeof(Item) * types->length);
    
    for (int i = 0; i < types->length; i++) {
        type_list->items[i] = (Item){.item = (uint64_t)types->data[i]};
    }
    
    arraylist_free(types);
    return create_union_schema(type_list, parser->pool);
}

TypeSchema* build_array_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // Get the first child as element type
    TSNode element_node = ts_node_named_child(node, 0);
    TypeSchema* element_type = build_schema_type(parser, element_node);
    
    if (!element_type) {
        element_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
    
    return create_array_schema(element_type, 0, -1, parser->pool);
}

TypeSchema* build_map_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    //printf("[SCHEMA_PARSER] DEBUG: Building map schema from node\n");
    
    // For runtime map nodes, we also need to parse field definitions
    // This handles the runtime map literal syntax: { key: value, ... }
    TypeSchema* schema = create_map_schema(
        create_primitive_schema(LMD_TYPE_STRING, parser->pool),
        create_primitive_schema(LMD_TYPE_ANY, parser->pool),
        parser->pool
    );
    
    SchemaMap* map_data = (SchemaMap*)schema->schema_data;
    if (!map_data) {
        return schema;
    }
    
    // Parse map items from child nodes
    SchemaMapField* first_field = NULL;
    SchemaMapField* last_field = NULL;
    int field_count = 0;
    
    uint32_t child_count = ts_node_child_count(node);
    //printf("[SCHEMA_PARSER] DEBUG: Map node has %d children\n", child_count);
    
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_symbol = ts_node_symbol(child);
        
        // Look for map_item nodes which should contain field definitions
        if (child_symbol == sym_map_item) {
            TSNode name_node = ts_node_child_by_field_id(child, FIELD_NAME);
            TSNode type_node = ts_node_child_by_field_id(child, FIELD_TYPE);
            
            // If field IDs don't work, try sequential parsing
            if (ts_node_is_null(name_node) || ts_node_is_null(type_node)) {
                uint32_t item_child_count = ts_node_child_count(child);
                for (uint32_t j = 0; j < item_child_count; j++) {
                    TSNode item_child = ts_node_child(child, j);
                    const char* item_child_type = ts_node_type(item_child);
                    
                    if (strcmp(item_child_type, "identifier") == 0 && ts_node_is_null(name_node)) {
                        name_node = item_child;
                    } else if (j > 0 && !ts_node_is_null(name_node) && ts_node_is_null(type_node)) {
                        if (strcmp(item_child_type, "identifier") == 0 || 
                            strstr(item_child_type, "type") != NULL) {
                            type_node = item_child;
                        }
                    }
                }
            }
            
            if (!ts_node_is_null(name_node) && !ts_node_is_null(type_node)) {
                // Extract field name and copy it to memory pool for persistence
                StrView field_name_view = get_node_source(parser, name_node);
                String* field_name_str = string_from_strview(field_name_view, parser->pool);
                StrView field_name = {
                    .str = field_name_str->chars,
                    .length = field_name_str->len
                };
                TypeSchema* field_type = build_schema_type(parser, type_node);
                
                if (!field_type) {
                    field_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
                }
                
                SchemaMapField* field = (SchemaMapField*)pool_calloc(parser->pool, sizeof(SchemaMapField));
                field->name = field_name;
                field->type = field_type;
                field->required = true;  // Map literals assume required by default
                field->next = NULL;
                
                if (!first_field) {
                    first_field = field;
                    last_field = field;
                } else {
                    last_field->next = field;
                    last_field = field;
                }
                field_count++;
            }
        }
    }
    
    map_data->fields = first_field;
    map_data->field_count = field_count;
    
    //printf("[SCHEMA_PARSER] DEBUG: Map schema created with %d fields\n", field_count);
    
    return schema;
}

TypeSchema* build_element_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // Extract element tag name
    // TODO: Parse actual element structure
    return create_element_schema("element", parser->pool);
}

TypeSchema* build_occurrence_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    TypeSchema* base_type = create_primitive_schema(LMD_TYPE_STRING, parser->pool);
    return create_occurrence_schema(base_type, 0, 1, parser->pool);
}

TypeSchema* build_reference_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // Extract type name from identifier using the helper function
    StrView type_name = get_node_source(parser, node);
    
    printf("[SCHEMA_DEBUG] build_reference_schema: type_name='%.*s', length=%zu\n", 
           (int)type_name.length, type_name.str, type_name.length);
    
    // Check if this is a primitive type name first
    if (type_name.length > 0) {
        // Check for primitive type names
        if (strview_equal(&type_name, "int")) {
            return create_primitive_schema(LMD_TYPE_INT, parser->pool);
        } else if (strview_equal(&type_name, "float")) {
            return create_primitive_schema(LMD_TYPE_FLOAT, parser->pool);
        } else if (strview_equal(&type_name, "string")) {
            return create_primitive_schema(LMD_TYPE_STRING, parser->pool);
        } else if (strview_equal(&type_name, "bool")) {
            return create_primitive_schema(LMD_TYPE_BOOL, parser->pool);
        } else if (strview_equal(&type_name, "null")) {
            printf("[SCHEMA_DEBUG] build_reference_schema: creating LMD_TYPE_NULL for 'null'\n");
            return create_primitive_schema(LMD_TYPE_NULL, parser->pool);
        } else if (strview_equal(&type_name, "char")) {
            return create_primitive_schema(LMD_TYPE_SYMBOL, parser->pool);  // char represented as symbol
        } else if (strview_equal(&type_name, "symbol")) {
            return create_primitive_schema(LMD_TYPE_SYMBOL, parser->pool);
        } else if (strview_equal(&type_name, "datetime")) {
            return create_primitive_schema(LMD_TYPE_DTIME, parser->pool);
        } else if (strview_equal(&type_name, "decimal")) {
            return create_primitive_schema(LMD_TYPE_DECIMAL, parser->pool);
        } else if (strview_equal(&type_name, "binary")) {
            return create_primitive_schema(LMD_TYPE_BINARY, parser->pool);
        } else if (strview_equal(&type_name, "any")) {
            return create_primitive_schema(LMD_TYPE_ANY, parser->pool);
        }
    }
    
    // Not a primitive type, create a type reference
    // Convert StrView to null-terminated string
    char* name_str = (char*)pool_calloc(parser->pool, type_name.length + 1);
    memcpy(name_str, type_name.str, type_name.length);
    name_str[type_name.length] = '\0';
    
    printf("[SCHEMA_DEBUG] build_reference_schema: creating reference for '%s' (not a primitive)\n", name_str);
    
    return create_reference_schema(name_str, parser->pool);
}

// ==================== Enhanced Type Building Functions ====================

TypeSchema* build_list_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // Get the first child as element type
    TSNode element_node = ts_node_named_child(node, 0);
    TypeSchema* element_type = build_schema_type(parser, element_node);
    
    if (!element_type) {
        element_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
    
    return create_array_schema(element_type, 0, -1, parser->pool);
}

TypeSchema* build_object_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // For now, create a simple string -> any map
    // TODO: Parse actual field definitions from the object node using field IDs
    TypeSchema* key_type = create_primitive_schema(LMD_TYPE_STRING, parser->pool);
    TypeSchema* value_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    
    return create_map_schema(key_type, value_type, parser->pool);
}

TypeSchema* build_function_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // Extract function parameters and return type using field IDs
    // TODO: Parse actual function signature from the node
    return create_primitive_schema(LMD_TYPE_ANY, parser->pool);
}

TypeSchema* build_primary_type_schema(SchemaParser* parser, TSNode node) {
    return build_primary_type_schema_with_depth(parser, node, 0);
}

TypeSchema* build_primary_type_schema_with_depth(SchemaParser* parser, TSNode node, int depth) {
    if (!parser || depth > 10) {
        printf("[SCHEMA_DEBUG] build_primary_type_schema: maximum recursion depth reached, defaulting to ANY\n");
        return create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
    
    printf("[SCHEMA_DEBUG] build_primary_type_schema: depth=%d, symbol=%d, type='%s'\n", 
           depth, ts_node_symbol(node), ts_node_type(node));
    
    // First, check all children (not just named children) for primitive type tokens
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_symbol = ts_node_symbol(child);
        
        printf("[SCHEMA_DEBUG] build_primary_type_schema: child[%d] symbol=%d, type='%s'\n", 
               i, child_symbol, ts_node_type(child));
        
        // Check for specific primitive type anonymous symbols first
        switch (child_symbol) {
            case 64: // anon_sym_int
                printf("[SCHEMA_DEBUG] build_primary_type_schema: found anon_sym_int=64\n");
                return create_primitive_schema(LMD_TYPE_INT, parser->pool);
            case 67: // anon_sym_string
                printf("[SCHEMA_DEBUG] build_primary_type_schema: found anon_sym_string=67\n");
                return create_primitive_schema(LMD_TYPE_STRING, parser->pool);
            case 65: // anon_sym_float
                printf("[SCHEMA_DEBUG] build_primary_type_schema: found anon_sym_float=65\n");
                return create_primitive_schema(LMD_TYPE_FLOAT, parser->pool);
            case 97: // anon_sym_bool
                printf("[SCHEMA_DEBUG] build_primary_type_schema: found anon_sym_bool=97\n");
                return create_primitive_schema(LMD_TYPE_BOOL, parser->pool);
            case 21: // anon_sym_null
                printf("[SCHEMA_DEBUG] build_primary_type_schema: found anon_sym_null=21\n");
                return create_primitive_schema(LMD_TYPE_NULL, parser->pool);
        }
        
        // Handle identifiers (custom types)
        if (child_symbol == sym_identifier) {
            printf("[SCHEMA_DEBUG] build_primary_type_schema: found identifier, calling build_reference_schema\n");
            return build_reference_schema(parser, child);
        }
        
        // For non-wrapper types, delegate immediately
        if (child_symbol != sym_base_type && child_symbol != sym_primary_type) {
            printf("[SCHEMA_DEBUG] build_primary_type_schema: found non-wrapper type, delegating to build_schema_type\n");
            return build_schema_type(parser, child);
        }
    }
    
    // If we only found wrapper types, recurse into the first one with increased depth
    TSNode child = ts_node_named_child(node, 0);
    if (!ts_node_is_null(child)) {
        TSSymbol child_symbol = ts_node_symbol(child);
        
        if (child_symbol == sym_base_type || child_symbol == sym_primary_type) {
            printf("[SCHEMA_DEBUG] build_primary_type_schema: recursively processing wrapper type with depth %d\n", depth + 1);
            return build_primary_type_schema_with_depth(parser, child, depth + 1);
        }
    }
    
    printf("[SCHEMA_DEBUG] build_primary_type_schema: no resolvable child found, defaulting to LMD_TYPE_ANY\n");
    return create_primitive_schema(LMD_TYPE_ANY, parser->pool);
}

TypeSchema* build_list_type_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // Extract element type from list type using field IDs
    TSNode element_node = ts_node_child_by_field_id(node, field_type);
    TypeSchema* element_type = NULL;
    
    if (!ts_node_is_null(element_node)) {
        element_type = build_schema_type(parser, element_node);
    }
    
    if (!element_type) {
        element_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
    
    return create_array_schema(element_type, 0, -1, parser->pool);
}

TypeSchema* build_array_type_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    printf("[SCHEMA_DEBUG] build_array_type_schema: parsing array type node\n");
    
    // For array types [ElementType], we need to find the element type within the brackets
    // Try field IDs first
    TSNode element_node = ts_node_child_by_field_id(node, field_type);
    TypeSchema* element_type = NULL;
    
    if (ts_node_is_null(element_node)) {
        // If field ID doesn't work, look for child nodes manually
        uint32_t child_count = ts_node_child_count(node);
        printf("[SCHEMA_DEBUG] build_array_type_schema: array has %d children\n", child_count);
        
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            const char* child_type = ts_node_type(child);
            TSSymbol child_symbol = ts_node_symbol(child);
            
            printf("[SCHEMA_DEBUG] build_array_type_schema: child[%d] type='%s', symbol=%d\n", 
                   i, child_type, child_symbol);
            
            // Skip brackets and other syntax tokens, look for actual type nodes
            if (child_symbol != anon_sym_LBRACK && child_symbol != anon_sym_RBRACK &&
                child_symbol != anon_sym_STAR && child_symbol != anon_sym_PLUS &&
                child_symbol != anon_sym_QMARK) {
                element_node = child;
                printf("[SCHEMA_DEBUG] build_array_type_schema: found element type node: %s\n", child_type);
                break;
            }
        }
    }
    
    if (!ts_node_is_null(element_node)) {
        element_type = build_schema_type(parser, element_node);
        printf("[SCHEMA_DEBUG] build_array_type_schema: element_type=%p, schema_type=%d\n", 
               element_type, element_type ? element_type->schema_type : -1);
    }
    
    if (!element_type) {
        printf("[SCHEMA_DEBUG] build_array_type_schema: defaulting to LMD_TYPE_ANY\n");
        element_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
    
    TypeSchema* result = create_array_schema(element_type, 0, -1, parser->pool);
    printf("[SCHEMA_DEBUG] build_array_type_schema: created array schema=%p\n", result);
    return result;
}

TypeSchema* build_map_type_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
        // //printf("[SCHEMA_PARSER] DEBUG: Building map type schema from node\n");    // Create map schema with proper field parsing
    TypeSchema* schema = create_map_schema(
        create_primitive_schema(LMD_TYPE_STRING, parser->pool), 
        create_primitive_schema(LMD_TYPE_ANY, parser->pool), 
        parser->pool
    );
    
    SchemaMap* map_data = (SchemaMap*)schema->schema_data;
    if (!map_data) {
        printf("[SCHEMA_PARSER] ERROR: Failed to create map schema data\n");
        return schema;
    }
    
    // Parse field definitions from child nodes
    SchemaMapField* first_field = NULL;
    SchemaMapField* last_field = NULL;
    int field_count = 0;
    
    uint32_t child_count = ts_node_child_count(node);
    //printf("[SCHEMA_PARSER] DEBUG: Map type node has %d children\n", child_count);
    
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_symbol = ts_node_symbol(child);
        
        //printf("[SCHEMA_PARSER] DEBUG: Child %d: symbol=%d (%s)\n", i, child_symbol, ts_node_type(child));
        
        // Look for map_type_item nodes which should contain field definitions
        if (child_symbol == sym_map_type_item) {
            //printf("[SCHEMA_PARSER] DEBUG: Found map_type_item, parsing field\n");
            
            // Get field name and type from the map_type_item
            TSNode name_node = ts_node_child_by_field_id(child, FIELD_NAME);
            TSNode type_node = ts_node_child_by_field_id(child, FIELD_TYPE);
            
            if (ts_node_is_null(name_node) || ts_node_is_null(type_node)) {
                //printf("[SCHEMA_PARSER] DEBUG: Looking for fields in child nodes\n");
                // Try to find name and type in child nodes if field IDs don't work
                uint32_t item_child_count = ts_node_child_count(child);
                for (uint32_t j = 0; j < item_child_count; j++) {
                    TSNode item_child = ts_node_child(child, j);
                    const char* item_child_type = ts_node_type(item_child);
                    //printf("[SCHEMA_PARSER] DEBUG: Item child %d: %s\n", j, item_child_type);
                    
                    if (strcmp(item_child_type, "identifier") == 0 && ts_node_is_null(name_node)) {
                        name_node = item_child;
                    } else if (j > 0 && !ts_node_is_null(name_node) && ts_node_is_null(type_node)) {
                        // Type should be after the identifier (and colon)
                        if (strcmp(item_child_type, "identifier") == 0 || 
                            strstr(item_child_type, "type") != NULL) {
                            type_node = item_child;
                        }
                    }
                }
            }
            
            if (!ts_node_is_null(name_node) && !ts_node_is_null(type_node)) {
                // Extract field name and copy it to memory pool for persistence
                StrView field_name_view = get_node_source(parser, name_node);
                String* field_name_str = string_from_strview(field_name_view, parser->pool);
                StrView field_name = {
                    .str = field_name_str->chars,
                    .length = field_name_str->len
                };
                //printf("[SCHEMA_PARSER] DEBUG: Field name: %.*s\n", (int)field_name.length, field_name.str);
                
                // Parse field type
                TypeSchema* field_type = build_schema_type(parser, type_node);
                printf("[SCHEMA_DEBUG] build_map_type_schema: field='%.*s', field_type=%p\n",
                       (int)field_name.length, field_name.str, field_type);
                if (!field_type) {
                    printf("[SCHEMA_DEBUG] build_map_type_schema: field_type is NULL, using LMD_TYPE_ANY\n");
                    field_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
                }
                
                // Determine if field is required (assume required by default, check for optional modifiers)
                bool required = true;
                if (field_type && field_type->schema_type == LMD_SCHEMA_OCCURRENCE) {
                    SchemaOccurrence* occ = (SchemaOccurrence*)field_type->schema_data;
                    if (occ && (occ->modifier == '?' || occ->modifier == '*')) {
                        required = false;
                    }
                }
                
                // Create SchemaMapField
                SchemaMapField* field = (SchemaMapField*)pool_calloc(parser->pool, sizeof(SchemaMapField));
                field->name = field_name;
                field->type = field_type;
                field->required = required;
                field->next = NULL;
                
                // Add to fields list
                if (!first_field) {
                    first_field = field;
                    last_field = field;
                } else {
                    last_field->next = field;
                    last_field = field;
                }
                field_count++;
                
                //printf("[SCHEMA_PARSER] DEBUG: Added field '%.*s' (required: %s)\n", 
                //       (int)field_name.length, field_name.str, required ? "yes" : "no");
            } else {
                printf("[SCHEMA_PARSER] WARNING: Could not extract name/type from map_type_item\n");
            }
        }
    }
    
    // Update map schema with parsed fields
    map_data->fields = first_field;
    map_data->field_count = field_count;
    
    //printf("[SCHEMA_PARSER] DEBUG: Map schema created with %d fields\n", field_count);
    
    return schema;
}

TypeSchema* build_element_type_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // Extract element tag name first
    const char* tag_name = "element";
    TSNode name_node = ts_node_child_by_field_id(node, field_name);
    
    if (!ts_node_is_null(name_node)) {
        StrView name_view = get_node_source(parser, name_node);
        char* name_str = (char*)pool_calloc(parser->pool, name_view.length + 1);
        memcpy(name_str, name_view.str, name_view.length);
        name_str[name_view.length] = '\0';
        tag_name = name_str;
    } else {
        // Look for an identifier child which should be the tag name
        for (int i = 0; i < ts_node_child_count(node); i++) {
            TSNode child = ts_node_child(node, i);
            const char* child_type = ts_node_type(child);
            if (strcmp(child_type, "identifier") == 0) {
                StrView tag_view = get_node_source(parser, child);
                char* parsed_tag = (char*)pool_calloc(parser->pool, tag_view.length + 1);
                memcpy(parsed_tag, tag_view.str, tag_view.length);
                parsed_tag[tag_view.length] = '\0';
                tag_name = parsed_tag;
                break;
            }
        }
    }
    
    // Create the basic element schema
    TypeSchema* schema = create_element_schema(tag_name, parser->pool);
    SchemaElement* element_data = (SchemaElement*)schema->schema_data;
    
    // Parse attributes from child nodes
    SchemaMapField* attributes = NULL;
    SchemaMapField* last_attr = NULL;
    int attr_count = 0;
    
    for (int i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_symbol = ts_node_symbol(child);
        
        // Skip the tag name identifier
        if (child_symbol == sym_identifier) continue;
        
        // Parse assign expressions as attributes (name: type format)
        if (child_symbol == sym_assign_expr || child_symbol == sym_attr) {
            TSNode key_node = {0};
            TSNode type_node = {0};
            
            if (child_symbol == sym_attr) {
                // For attr nodes, get name and type from child nodes directly
                if (ts_node_child_count(child) >= 3) {
                    key_node = ts_node_child(child, 0);  // identifier (name)
                    type_node = ts_node_child(child, 2); // type
                }
            } else {
                // For assign_expr nodes, use field IDs
                key_node = ts_node_child_by_field_id(child, field_name);
                type_node = ts_node_child_by_field_id(child, field_type);
            }
            
            if (!ts_node_is_null(key_node) && !ts_node_is_null(type_node)) {
                // Extract attribute name
                StrView attr_name = get_node_source(parser, key_node);
                char* attr_name_str = (char*)pool_calloc(parser->pool, attr_name.length + 1);
                memcpy(attr_name_str, attr_name.str, attr_name.length);
                attr_name_str[attr_name.length] = '\0';
                
                // Parse attribute type schema
                TypeSchema* attr_type = build_schema_type(parser, type_node);
                
                // Determine if attribute is required (no ? or * modifier)
                bool required = true;
                if (attr_type && attr_type->schema_type == LMD_SCHEMA_OCCURRENCE) {
                    SchemaOccurrence* occ = (SchemaOccurrence*)attr_type->schema_data;
                    if (occ->modifier == '?' || occ->modifier == '*') {
                        required = false;
                    }
                }
                
                // Create SchemaMapField for this attribute
                SchemaMapField* attr_field = (SchemaMapField*)pool_calloc(parser->pool, sizeof(SchemaMapField));
                attr_field->name = strview_from_cstr(attr_name_str);
                attr_field->type = attr_type;
                attr_field->required = required;
                attr_field->next = NULL;
                
                // Add to attributes list
                if (!attributes) {
                    attributes = attr_field;
                    last_attr = attr_field;
                } else {
                    last_attr->next = attr_field;
                    last_attr = attr_field;
                }
                attr_count++;
            }
        }
    }
    
    // Set the parsed attributes
    element_data->attributes = attributes;
    
    return schema;
}

TypeSchema* build_function_type_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // Extract function parameters and return type using field IDs
    // TODO: Parse actual function type signature from the node
    return create_primitive_schema(LMD_TYPE_ANY, parser->pool);
}

TypeSchema* build_binary_type_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // Handle binary type expressions (e.g., union types with |)
    TSNode left_node = ts_node_child_by_field_id(node, field_left);
    TSNode right_node = ts_node_child_by_field_id(node, field_right);
    TSNode operator_node = ts_node_child_by_field_id(node, field_operator);
    
    if (ts_node_is_null(left_node) || ts_node_is_null(right_node)) {
        return create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
    
    StrView op = get_node_source(parser, operator_node);
    if (strview_equal(&op, "|")) {
        // Union type
        ArrayList* types = arraylist_new(2);
        
        TypeSchema* left_type = build_schema_type(parser, left_node);
        TypeSchema* right_type = build_schema_type(parser, right_node);
        
        if (left_type) arraylist_append(types, left_type);
        if (right_type) arraylist_append(types, right_type);
        
        // Convert ArrayList to List for create_union_schema
        List* type_list = (List*)pool_calloc(parser->pool, sizeof(List));
        type_list->length = types->length;
        type_list->capacity = types->length;
        type_list->items = (Item*)pool_calloc(parser->pool, sizeof(Item) * types->length);
        
        for (int i = 0; i < types->length; i++) {
            type_list->items[i] = (Item){.item = (uint64_t)types->data[i]};
        }
        
        arraylist_free(types);
        return create_union_schema(type_list, parser->pool);
    }
    
    return NULL;
}

// ==================== New Helper Functions ====================

void parse_all_type_definitions(SchemaParser* parser, TSNode root) {
    if (!parser || ts_node_is_null(root)) return;
    
    // Clear existing definitions
    if (parser->type_definitions) {
        arraylist_clear(parser->type_definitions);
    } else {
        parser->type_definitions = arraylist_new(16);
    }
    
    // Walk through all children looking for type definitions
    uint32_t child_count = ts_node_child_count(root);
    
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(root, i);
        TSSymbol child_symbol = ts_node_symbol(child);
        const char* child_type = ts_node_type(child);
        
        // Look for type_stam nodes (which contain type definitions)
        if (child_symbol == sym_type_stam) {
            TypeDefinition* def = build_type_definition(parser, child);
            if (def) {
                arraylist_append(parser->type_definitions, def);
            }
        } else {
            // Recursively search in child nodes for nested type definitions
            parse_all_type_definitions_recursive(parser, child);
        }
        
        // Also recursively search in children for nested type definitions
        parse_all_type_definitions_recursive(parser, child);
    }
}

void parse_all_type_definitions_recursive(SchemaParser* parser, TSNode node) {
    if (!parser || ts_node_is_null(node)) return;
    
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char* child_type = ts_node_type(child);
        
        // Look for type_stam nodes (which contain type definitions)
        TSSymbol child_symbol = ts_node_symbol(child);
        if (child_symbol == sym_type_stam) {
            TypeDefinition* def = build_type_definition(parser, child);
            if (def) {
                arraylist_append(parser->type_definitions, def);
            }
        }
        
        // Continue recursively
        parse_all_type_definitions_recursive(parser, child);
    }
}

TypeSchema* find_type_definition(SchemaParser* parser, const char* type_name) {
    if (!parser || !type_name || !parser->type_definitions) return NULL;
    
    size_t name_len = strlen(type_name);
    
    for (long i = 0; i < parser->type_definitions->length; i++) {
        TypeDefinition* def = (TypeDefinition*)parser->type_definitions->data[i];
        if (def && def->name.length == name_len && 
            memcmp(def->name.str, type_name, name_len) == 0) {
            return def->schema_type;
        }
    }
    
    return NULL;
}

// Missing function implementation
TypeSchema* build_binary_expression_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // Handle binary expressions in type context (delegate to binary_type_schema)
    return build_binary_type_schema(parser, node);
}
