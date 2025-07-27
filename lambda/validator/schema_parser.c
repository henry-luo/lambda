/**
 * @file schema_parser.c
 * @brief Lambda Schema Parser Implementation - Tree-sitter Integration
 * @author Henry Luo
 * @license MIT
 */

#include "validator.h"
#include "../ts-enum.h"  // Include Tree-sitter symbols and field IDs
#include <string.h>
#include <assert.h>

// External function declarations
TSParser* lambda_parser(void);
TSTree* lambda_parse_source(TSParser* parser, const char* source_code);

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
    if (!tree) return NULL;
    
    // Store source and tree for later use
    parser->current_source = source;
    parser->current_tree = tree;
    
    TSNode root = ts_tree_root_node(tree);
    
    // Build schema from the root node
    TypeSchema* schema = build_schema_type(parser, root);
    
    return schema;
}

TypeDefinition* build_type_definition(SchemaParser* parser, TSNode type_node) {
    if (!parser) return NULL;
    
    TypeDefinition* def = (TypeDefinition*)pool_calloc(parser->pool, sizeof(TypeDefinition));
    def->source_node = type_node;
    
    // Extract type name from node using field ID
    TSNode name_node = ts_node_child_by_field_id(type_node, field_name);
    if (!ts_node_is_null(name_node)) {
        def->name = get_node_source(parser, name_node);
    } else {
        def->name = strview_from_cstr("UnnamedType");
    }
    
    // Build schema type from type expression using field ID
    TSNode type_expr_node = ts_node_child_by_field_id(type_node, field_type);
    if (!ts_node_is_null(type_expr_node)) {
        def->schema_type = build_schema_type(parser, type_expr_node);
    } else {
        def->schema_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
    
    def->is_exported = true;
    
    return def;
}

TypeSchema* build_schema_type(SchemaParser* parser, TSNode type_expr_node) {
    if (!parser || ts_node_is_null(type_expr_node)) return NULL;
    
    TSSymbol symbol = ts_node_symbol(type_expr_node);
    
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
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_STRING);
            
        case anon_sym_symbol:
        case sym_symbol:
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_STRING);
            
        case anon_sym_datetime:
        case sym_datetime:
        case anon_sym_date:
        case anon_sym_time:
        case sym_time:
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_STRING);
            
        case anon_sym_decimal:
        case sym_decimal:
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_FLOAT);
            
        case anon_sym_binary:
        case sym_binary:
            return build_primitive_schema(parser, type_expr_node, LMD_TYPE_STRING);
            
        case anon_sym_null:
        case sym_null:
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
            
            // Default to any type
            return create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
}

// ==================== Type Building Functions ====================

TypeSchema* build_primitive_schema(SchemaParser* parser, TSNode node, TypeId type_id) {
    if (!parser) return NULL;
    
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
        type_list->items[i] = (Item)types->data[i];
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
    
    // For now, create a simple string -> any map
    // TODO: Parse actual field definitions from the map node
    TypeSchema* key_type = create_primitive_schema(LMD_TYPE_STRING, parser->pool);
    TypeSchema* value_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    
    return create_map_schema(key_type, value_type, parser->pool);
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
    
    // Convert StrView to null-terminated string
    char* name_str = (char*)pool_calloc(parser->pool, type_name.length + 1);
    memcpy(name_str, type_name.str, type_name.length);
    name_str[type_name.length] = '\0';
    
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
    if (!parser) return NULL;
    
    // Handle primary type expressions - delegate to first child
    TSNode child = ts_node_named_child(node, 0);
    if (!ts_node_is_null(child)) {
        return build_schema_type(parser, child);
    }
    
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
    
    // Extract element type from array type using field IDs
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

TypeSchema* build_map_type_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // Extract key and value types from map type using field IDs
    // TODO: Parse actual map type structure from the node
    TypeSchema* key_type = create_primitive_schema(LMD_TYPE_STRING, parser->pool);
    TypeSchema* value_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    
    return create_map_schema(key_type, value_type, parser->pool);
}

TypeSchema* build_element_type_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // Extract element tag name and attributes using field IDs
    TSNode name_node = ts_node_child_by_field_id(node, field_name);
    const char* tag_name = "element";
    
    if (!ts_node_is_null(name_node)) {
        StrView name_view = get_node_source(parser, name_node);
        char* name_str = (char*)pool_calloc(parser->pool, name_view.length + 1);
        memcpy(name_str, name_view.str, name_view.length);
        name_str[name_view.length] = '\0';
        tag_name = name_str;
    }
    
    return create_element_schema(tag_name, parser->pool);
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
            type_list->items[i] = (Item)types->data[i];
        }
        
        arraylist_free(types);
        return create_union_schema(type_list, parser->pool);
    }
    
    // Other binary type operations
    return create_primitive_schema(LMD_TYPE_ANY, parser->pool);
}

TypeSchema* build_binary_expression_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // Handle binary expressions in type context (delegate to binary_type_schema)
    return build_binary_type_schema(parser, node);
}
