/**
 * @file schema_par    // Initialize base transpiler components (accessing Script fields directly)
    parser->base.ast_pool = pool;
    parser->base.parser = lambda_parser();  // Use the same Tree-sitter parser
    parser->base.const_list = arraylist_new(64);
    parser->base.type_list = arraylist_new(32);
    
    // Initialize name scope for type definitions
    NameScope* global_scope = (NameScope*)pool_calloc(pool, sizeof(NameScope));
    parser->base.current_scope = global_scope;@brief Lambda Schema Parser Implementation
 * @author Henry Luo
 * @license MIT
 */

#include "validator.h"
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
    
    // Extract type name from node using the helper function
    TSNode name_node = ts_node_child_by_field_id(type_node, FIELD_NAME);
    if (!ts_node_is_null(name_node)) {
        def->name = get_node_source(parser, name_node);
    } else {
        def->name = strview_from_cstr("UnnamedType");
    }
    
    // Build schema type from type expression
    TSNode type_expr_node = ts_node_child_by_field_id(type_node, FIELD_TYPE);
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
    
    // Handle different node types based on Tree-sitter symbols
    if (symbol == SYM_INT || symbol == SYM_FLOAT || symbol == SYM_STRING || 
        symbol == SYM_NULL || symbol == SYM_TRUE || symbol == SYM_FALSE) {
        return build_primitive_schema(parser, type_expr_node);
    }
    else if (symbol == SYM_ARRAY) {
        return build_array_schema(parser, type_expr_node);
    }
    else if (symbol == SYM_MAP) {
        return build_map_schema(parser, type_expr_node);
    }
    else if (symbol == SYM_ELEMENT) {
        return build_element_schema(parser, type_expr_node);
    }
    else if (symbol == SYM_IDENT) {
        return build_reference_schema(parser, type_expr_node);
    }
        // Handle binary expressions for union types (Type1 | Type2)
    else if (ts_node_child_count(type_expr_node) >= 3) {
        TSNode op_node = ts_node_child(type_expr_node, 1);
        StrView op = get_node_source(parser, op_node);
        if (strview_equal(&op, "|")) {
            return build_union_schema(parser, type_expr_node);
        }
    }
    
    // Default to any type
    return create_primitive_schema(LMD_TYPE_ANY, parser->pool);
}

// ==================== Type Building Functions ====================

TypeSchema* build_primitive_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    TSSymbol symbol = ts_node_symbol(node);
    TypeId primitive_type = LMD_TYPE_ANY;
    
    if (symbol == SYM_INT) {
        primitive_type = LMD_TYPE_INT;
    } else if (symbol == SYM_FLOAT) {
        primitive_type = LMD_TYPE_FLOAT;
    } else if (symbol == SYM_STRING) {
        primitive_type = LMD_TYPE_STRING;
    } else if (symbol == SYM_NULL) {
        primitive_type = LMD_TYPE_NULL;
    } else if (symbol == SYM_TRUE || symbol == SYM_FALSE) {
        primitive_type = LMD_TYPE_BOOL;
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
        type_list->items[i] = (Item)arraylist_nth(types, i);
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
