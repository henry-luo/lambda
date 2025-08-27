/**
 * @file schema_parser.c
 * @brief Lambda Schema Parser Implementation - Tree-sitter Integration
 * @author Henry Luo
 * @license MIT
 */

#include "validator.hpp"
#include "../ast.hpp"      // Include field constants
#include "../../lib/arraylist.h"
#include "../../lib/strview.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

// Debug flag - set to 0 to disable all SCHEMA_DEBUG output
#define ENABLE_SCHEMA_DEBUG 0

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
    if (!parser || ts_node_is_null(type_node)) return NULL;
    
    TypeDefinition* def = (TypeDefinition*)pool_calloc(parser->pool, sizeof(TypeDefinition));
    if (!def) return NULL;
    
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
            if (!def->schema_type) {
                def->schema_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
            }
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

// Recursion depth tracking to prevent infinite recursion
static int schema_parse_depth = 0;
static const int MAX_SCHEMA_PARSE_DEPTH = 50;

TypeSchema* build_schema_type(SchemaParser* parser, TSNode type_expr_node) {
    if (!parser || ts_node_is_null(type_expr_node)) return NULL;
    
    // Additional safety check for invalid nodes
    if (!ts_node_is_named(type_expr_node) && ts_node_child_count(type_expr_node) == 0) {
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_schema_type: invalid/empty node, returning ANY schema\n");
        return create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
    
    // Prevent infinite recursion
    if (schema_parse_depth >= MAX_SCHEMA_PARSE_DEPTH) {
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_schema_type: max recursion depth reached\n");
        return create_primitive_schema(LMD_TYPE_STRING, parser->pool);
    }
    
    schema_parse_depth++;
    
    TSSymbol symbol = ts_node_symbol(type_expr_node);
    
    // Check for ERROR nodes and handle them gracefully
    if (symbol == 65535) { // ERROR node symbol
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_schema_type: ERROR node detected, returning ANY schema\n");
        schema_parse_depth--;
        return create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
    
    const char* node_type = ts_node_type(type_expr_node);
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_schema_type: symbol=%d, node_type='%s', depth=%d\n", 
                                   symbol, node_type, schema_parse_depth);
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_schema_type: sym_base_type=%d, sym_primary_type=%d, sym_identifier=%d\n", 
           sym_base_type, sym_primary_type, sym_identifier);
    
    TypeSchema* result = NULL;
    
    // Handle different node types based on Tree-sitter symbols from ts-enum.h
    switch (symbol) {
        // Base type nodes
        case anon_sym_int:
        case sym_integer:
            result = build_primitive_schema(parser, type_expr_node, LMD_TYPE_INT);
            break;
            
        case anon_sym_float:
        case sym_float:
            result = build_primitive_schema(parser, type_expr_node, LMD_TYPE_FLOAT);
            break;
            
        case anon_sym_number:
            result = build_primitive_schema(parser, type_expr_node, LMD_TYPE_FLOAT);
            break;
            
        case anon_sym_string:
        case sym_string:
            result = build_primitive_schema(parser, type_expr_node, LMD_TYPE_STRING);
            break;
            
        case anon_sym_bool:
        case sym_true:
        case sym_false:
            result = build_primitive_schema(parser, type_expr_node, LMD_TYPE_BOOL);
            break;
            
        case anon_sym_char:
            result = build_primitive_schema(parser, type_expr_node, LMD_TYPE_STRING);  // Char is represented as string
            break;
            
        case anon_sym_symbol:
        case sym_symbol:
            result = build_primitive_schema(parser, type_expr_node, LMD_TYPE_SYMBOL);
            break;
            
        case anon_sym_datetime:
        case sym_datetime:
        case anon_sym_date:
        case anon_sym_time:
        case sym_time:
            result = build_primitive_schema(parser, type_expr_node, LMD_TYPE_DTIME);
            break;
            
        case anon_sym_decimal:
        case sym_decimal:
            result = build_primitive_schema(parser, type_expr_node, LMD_TYPE_DECIMAL);
            break;
            
        case anon_sym_binary:
        case sym_binary:
            result = build_primitive_schema(parser, type_expr_node, LMD_TYPE_BINARY);
            break;
            
        case anon_sym_null:
        case sym_null:
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] get_type_id: null case triggered\n");
            result = build_primitive_schema(parser, type_expr_node, LMD_TYPE_NULL);
            break;
            
        // Complex type nodes
        case anon_sym_list:
        case sym_list:
            result = build_list_schema(parser, type_expr_node);
            break;
            
        case anon_sym_array:
        case sym_array:
            result = build_array_schema(parser, type_expr_node);
            break;
            
        case anon_sym_map:
        case sym_map:
            result = build_map_schema(parser, type_expr_node);
            break;
            
        case anon_sym_element:
        case sym_element:
            result = build_element_schema(parser, type_expr_node);
            break;
            
        case anon_sym_object:
            result = build_object_schema(parser, type_expr_node);
            break;
            
        case anon_sym_function:
            result = build_function_schema(parser, type_expr_node);
            break;
            
        // Type expressions
        case sym_base_type:
        case sym_primary_type:
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_schema_type: sym_base_type/sym_primary_type case, calling build_primary_type_schema\n");
            result = build_primary_type_schema(parser, type_expr_node);
            break;
            
        case sym_list_type:
            result = build_list_type_schema(parser, type_expr_node);
            break;
            
        case sym_array_type:
            result = build_array_type_schema(parser, type_expr_node);
            break;
            
        case sym_map_type:
            result = build_map_type_schema(parser, type_expr_node);
            break;
            
        case sym_element_type:
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_schema_type: sym_element_type case matched, calling build_element_type_schema\n");
            result = build_element_type_schema(parser, type_expr_node);
            break;
        
        case sym_content_type:
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_schema_type: sym_content_type case matched\n");
            result = build_content_type_schema(parser, type_expr_node);
            break;
            
        case sym_fn_type:
            result = build_function_type_schema(parser, type_expr_node);
            break;
            
        case sym_binary_type:
            result = build_binary_type_schema(parser, type_expr_node);
            break;
            
        case sym_type_occurrence:
            result = build_occurrence_schema(parser, type_expr_node);
            break;
            
        // Identifiers and references
        case sym_identifier:
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_schema_type: sym_identifier case, calling build_reference_schema\n");
            result = build_reference_schema(parser, type_expr_node);
            break;
            
        // Binary expressions (for union types: Type1 | Type2)
        case sym_binary_expr:
            result = build_binary_expression_schema(parser, type_expr_node);
            break;
            
        default:
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_schema_type: default case for symbol=%d, type='%s'\n", symbol, node_type);
            // Handle binary expressions for union types manually if not caught above
            if (ts_node_child_count(type_expr_node) >= 3) {
                TSNode op_node = ts_node_child(type_expr_node, 1);
                StrView op = get_node_source(parser, op_node);
                if (strview_equal(&op, "|")) {
                    result = build_union_schema(parser, type_expr_node);
                    break;
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
            result = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
            break;
    }
    
    // Cleanup recursion depth and return result
    schema_parse_depth--;
    return result;
}

// ==================== Type Building Functions ====================

TypeSchema* build_primitive_schema(SchemaParser* parser, TSNode node, TypeId type_id) {
    if (!parser) return NULL;
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_primitive_schema: type_id=%d\n", type_id);
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

TypeSchema* build_content_type_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_content_type_schema: entering, child_count=%d\n", ts_node_child_count(node));
    
    // Content type should contain a single type expression (likely a string literal)
    if (ts_node_child_count(node) > 0) {
        TSNode child = ts_node_child(node, 0);
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_content_type_schema: processing child type='%s', symbol=%d\n", 
                                       ts_node_type(child), ts_node_symbol(child));
        return build_schema_type(parser, child);
    }
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_content_type_schema: no children, returning string schema\n");
    return create_primitive_schema(LMD_TYPE_STRING, parser->pool);
}

TypeSchema* build_element_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Parsing element_type node\n");
    
    // Extract element tag name from first identifier child
    const char* tag_name = "element"; // default
    TSNode tag_node = ts_node_named_child(node, 0);
    if (ts_node_symbol(tag_node) == sym_identifier) {
        StrView tag_view = get_node_source(parser, tag_node);
        char* tag_str = (char*)pool_calloc(parser->pool, tag_view.length + 1);
        memcpy(tag_str, tag_view.str, tag_view.length);
        tag_str[tag_view.length] = '\0';
        tag_name = tag_str;
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Element tag: '%s'\n", tag_name);
    }
    
    // Create element schema with proper tag name
    TypeSchema* schema = (TypeSchema*)pool_calloc(parser->pool, sizeof(TypeSchema));
    schema->schema_type = LMD_SCHEMA_ELEMENT;
    
    SchemaElement* element_data = (SchemaElement*)pool_calloc(parser->pool, sizeof(SchemaElement));
    element_data->tag = strview_from_cstr(tag_name);
    element_data->attributes = NULL;
    element_data->content_types = NULL;
    element_data->content_count = 0;
    element_data->is_open = true;
    
    // Parse attributes and content from the element_type node
    // Look for attr and content_type children
    int child_count = ts_node_named_child_count(node);
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Element has %d children\n", child_count);
    
    // Limit the number of children we process to prevent infinite loops
    int max_children = child_count < 100 ? child_count : 100;
    
    for (int i = 1; i < max_children; i++) { // Skip tag name (index 0)
        TSNode child = ts_node_named_child(node, i);
        if (ts_node_is_null(child)) {
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Child %d is null, skipping\n", i);
            continue;
        }
        
        uint16_t child_symbol = ts_node_symbol(child);
        
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Child %d symbol: %d\n", i, child_symbol);
        
        if (child_symbol == sym_attr) {
            // Parse attribute: name: type
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Parsing attribute\n");
            
            TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
            TSNode type_node = ts_node_child_by_field_name(child, "as", 2);
            
            if (!ts_node_is_null(name_node) && !ts_node_is_null(type_node)) {
                StrView attr_name = get_node_source(parser, name_node);
                
                // Validate attribute name length
                if (attr_name.length > 0 && attr_name.length < 256) {
                    TypeSchema* attr_type = build_schema_type(parser, type_node);
                    
                    if (attr_type) {
                        // Create new attribute field
                        SchemaMapField* attr_field = (SchemaMapField*)pool_calloc(parser->pool, sizeof(SchemaMapField));
                        if (attr_field) {
                            attr_field->name = attr_name;
                            attr_field->type = attr_type;
                            attr_field->required = true; // default for now
                            attr_field->next = element_data->attributes;
                            element_data->attributes = attr_field;
                            
                            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Added attribute '%.*s'\n", 
                                   (int)attr_name.length, attr_name.str);
                        }
                    }
                }
            }
        } else if (child_symbol == sym_content_type) {
            // Parse content types
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Parsing content_type\n");
            
            // Count number of content types in the content_type node
            int content_child_count = ts_node_named_child_count(child);
            if (content_child_count > 0) {
                element_data->content_count = content_child_count;
                element_data->content_types = (TypeSchema**)pool_calloc(parser->pool, 
                    sizeof(TypeSchema*) * content_child_count);
                
                for (int j = 0; j < content_child_count; j++) {
                    TSNode content_child = ts_node_named_child(child, j);
                    element_data->content_types[j] = build_schema_type(parser, content_child);
                    
                    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Added content type %d\n", j);
                }
            }
        } else {
            // Check if this child could be content (type references, identifiers, etc.)
            // Skip punctuation and comments
            const char* node_type_str = ts_node_type(child);
            if (child_symbol != sym_comment && 
                child_symbol != anon_sym_LT && child_symbol != anon_sym_GT &&
                child_symbol != 27 && child_symbol != 51 && child_symbol != 52 && child_symbol != 4 &&
                strcmp(node_type_str, ",") != 0 && strcmp(node_type_str, "ERROR") != 0) {
                
                if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Found potential content child: symbol=%d, type='%s'\n", 
                       child_symbol, node_type_str);
                
                // Try to parse this as content
                TypeSchema* content_schema = build_schema_type(parser, child);
                if (content_schema) {
                    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Successfully parsed content schema\n");
                    
                    // Add to content types (reallocate if needed)
                    element_data->content_count++;
                    if (element_data->content_types == NULL) {
                        element_data->content_types = (TypeSchema**)pool_calloc(parser->pool, 
                            sizeof(TypeSchema*) * element_data->content_count);
                    } else {
                        // Reallocate to accommodate new content
                        TypeSchema** new_content_types = (TypeSchema**)pool_calloc(parser->pool, 
                            sizeof(TypeSchema*) * element_data->content_count);
                        for (int k = 0; k < element_data->content_count - 1; k++) {
                            new_content_types[k] = element_data->content_types[k];
                        }
                        element_data->content_types = new_content_types;
                    }
                    element_data->content_types[element_data->content_count - 1] = content_schema;
                    
                    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Added content type %d (total: %d)\n", 
                           element_data->content_count - 1, element_data->content_count);
                }
            }
        }
    }
    
    schema->schema_data = element_data;
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Built element schema for '%s' with %d content types\n", 
           tag_name, element_data->content_count);
    
    return schema;
}

TypeSchema* build_occurrence_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Parsing type_occurrence node\n");
    
    // type_occurrence: base_type occurrence
    // First child should be the base type, second should be the occurrence
    if (ts_node_named_child_count(node) < 2) {
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: type_occurrence has insufficient children\n");
        return create_primitive_schema(LMD_TYPE_STRING, parser->pool);
    }
    
    TSNode base_type_node = ts_node_named_child(node, 0);
    TSNode occurrence_node = ts_node_named_child(node, 1);
    
    // Parse the base type
    TypeSchema* base_type = build_schema_type(parser, base_type_node);
    if (!base_type) {
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Failed to parse base type\n");
        base_type = create_primitive_schema(LMD_TYPE_STRING, parser->pool);
    }
    
    // Parse the occurrence modifier
    char modifier = '?'; // default
    long min_count = 0;
    long max_count = 1;
    
    if (ts_node_symbol(occurrence_node) == sym_occurrence) {
        StrView occ_text = get_node_source(parser, occurrence_node);
        if (occ_text.length > 0) {
            modifier = occ_text.str[0];
            
            switch (modifier) {
                case '?':
                    min_count = 0;
                    max_count = 1;
                    break;
                case '+':
                    min_count = 1;
                    max_count = -1; // unlimited
                    break;
                case '*':
                    min_count = 0;
                    max_count = -1; // unlimited
                    break;
                default:
                    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Unknown occurrence modifier '%c'\n", modifier);
                    modifier = '?';
                    min_count = 0;
                    max_count = 1;
                    break;
            }
            
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Occurrence modifier: '%c' (min=%ld, max=%ld)\n", 
                   modifier, min_count, max_count);
        }
    } else {
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Expected occurrence node, got symbol %d\n", 
               ts_node_symbol(occurrence_node));
    }
    
    // Create occurrence schema
    TypeSchema* schema = (TypeSchema*)pool_calloc(parser->pool, sizeof(TypeSchema));
    schema->schema_type = LMD_SCHEMA_OCCURRENCE;
    
    SchemaOccurrence* occ_data = (SchemaOccurrence*)pool_calloc(parser->pool, sizeof(SchemaOccurrence));
    occ_data->base_type = base_type;
    occ_data->modifier = modifier;
    
    schema->schema_data = occ_data;
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_PARSER] DEBUG: Built occurrence schema with modifier '%c'\n", modifier);
    
    return schema;
}

TypeSchema* build_reference_schema(SchemaParser* parser, TSNode node) {
    if (!parser) return NULL;
    
    // Extract type name from identifier using the helper function
    StrView type_name = get_node_source(parser, node);
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_reference_schema: type_name='%.*s', length=%zu\n", 
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
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_reference_schema: creating LMD_TYPE_NULL for 'null'\n");
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
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_reference_schema: creating reference for '%s' (not a primitive)\n", name_str);
    
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
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_primary_type_schema: maximum recursion depth reached, defaulting to ANY\n");
        return create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_primary_type_schema: depth=%d, symbol=%d, type='%s'\n", 
           depth, ts_node_symbol(node), ts_node_type(node));
    
    // First, check all children (not just named children) for primitive type tokens
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_symbol = ts_node_symbol(child);
        
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_primary_type_schema: child[%d] symbol=%d, type='%s'\n", 
               i, child_symbol, ts_node_type(child));
        
        // Check for specific primitive type anonymous symbols first
        switch (child_symbol) {
            case 64: // anon_sym_int
                if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_primary_type_schema: found anon_sym_int=64\n");
                return create_primitive_schema(LMD_TYPE_INT, parser->pool);
            case 67: // anon_sym_string
                if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_primary_type_schema: found anon_sym_string=67\n");
                return create_primitive_schema(LMD_TYPE_STRING, parser->pool);
            case 65: // anon_sym_float
                if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_primary_type_schema: found anon_sym_float=65\n");
                return create_primitive_schema(LMD_TYPE_FLOAT, parser->pool);
            case 97: // anon_sym_bool
                if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_primary_type_schema: found anon_sym_bool=97\n");
                return create_primitive_schema(LMD_TYPE_BOOL, parser->pool);
            case 21: // anon_sym_null
                if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_primary_type_schema: found anon_sym_null=21\n");
                return create_primitive_schema(LMD_TYPE_NULL, parser->pool);
        }
        
        // Handle identifiers (custom types)
        if (child_symbol == sym_identifier) {
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_primary_type_schema: found identifier, calling build_reference_schema\n");
            return build_reference_schema(parser, child);
        }
        
        // For non-wrapper types, delegate immediately
        if (child_symbol != sym_base_type && child_symbol != sym_primary_type) {
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_primary_type_schema: found non-wrapper type, delegating to build_schema_type\n");
            return build_schema_type(parser, child);
        }
    }
    
    // If we only found wrapper types, recurse into the first one with increased depth
    TSNode child = ts_node_named_child(node, 0);
    if (!ts_node_is_null(child)) {
        TSSymbol child_symbol = ts_node_symbol(child);
        
        if (child_symbol == sym_base_type || child_symbol == sym_primary_type) {
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_primary_type_schema: recursively processing wrapper type with depth %d\n", depth + 1);
            return build_primary_type_schema_with_depth(parser, child, depth + 1);
        }
    }
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_primary_type_schema: no resolvable child found, defaulting to LMD_TYPE_ANY\n");
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
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_array_type_schema: parsing array type node\n");
    
    // For array types [ElementType], we need to find the element type within the brackets
    // Try field IDs first
    TSNode element_node = ts_node_child_by_field_id(node, field_type);
    TypeSchema* element_type = NULL;
    
    if (ts_node_is_null(element_node)) {
        // If field ID doesn't work, look for child nodes manually
        uint32_t child_count = ts_node_child_count(node);
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_array_type_schema: array has %d children\n", child_count);
        
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            const char* child_type = ts_node_type(child);
            TSSymbol child_symbol = ts_node_symbol(child);
            
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_array_type_schema: child[%d] type='%s', symbol=%d\n", 
                   i, child_type, child_symbol);
            
            // Skip brackets and other syntax tokens, look for actual type nodes
            if (child_symbol != anon_sym_LBRACK && child_symbol != anon_sym_RBRACK &&
                child_symbol != anon_sym_STAR && child_symbol != anon_sym_PLUS &&
                child_symbol != anon_sym_QMARK) {
                element_node = child;
                if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_array_type_schema: found element type node: %s\n", child_type);
                break;
            }
        }
    }
    
    if (!ts_node_is_null(element_node)) {
        element_type = build_schema_type(parser, element_node);
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_array_type_schema: element_type=%p, schema_type=%d\n", 
               element_type, element_type ? element_type->schema_type : -1);
    }
    
    if (!element_type) {
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_array_type_schema: defaulting to LMD_TYPE_ANY\n");
        element_type = create_primitive_schema(LMD_TYPE_ANY, parser->pool);
    }
    
    TypeSchema* result = create_array_schema(element_type, 0, -1, parser->pool);
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_array_type_schema: created array schema=%p\n", result);
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
                if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_map_type_schema: field='%.*s', field_type=%p\n",
                       (int)field_name.length, field_name.str, field_type);
                if (!field_type) {
                    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_map_type_schema: field_type is NULL, using LMD_TYPE_ANY\n");
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
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_element_type_schema: entering, child_count=%d\n", ts_node_child_count(node));
    
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
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_element_type_schema: child[%d] type='%s', symbol=%d\n", 
                                           i, child_type, ts_node_symbol(child));
            if (strcmp(child_type, "identifier") == 0) {
                StrView tag_view = get_node_source(parser, child);
                char* parsed_tag = (char*)pool_calloc(parser->pool, tag_view.length + 1);
                memcpy(parsed_tag, tag_view.str, tag_view.length);
                parsed_tag[tag_view.length] = '\0';
                tag_name = parsed_tag;
                if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_element_type_schema: extracted tag_name='%s'\n", tag_name);
                break;
            }
        }
    }
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_element_type_schema: using tag_name='%s'\n", tag_name);
    
    // Create the basic element schema
    TypeSchema* schema = create_element_schema(tag_name, parser->pool);
    SchemaElement* element_data = (SchemaElement*)schema->schema_data;
    
    // Parse attributes from child nodes
    SchemaMapField* attributes = NULL;
    SchemaMapField* last_attr = NULL;
    int attr_count = 0;
    
    // NEW: Also look for content literals
    TypeSchema** content_types = NULL;
    int content_count = 0;
    
    for (int i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        TSSymbol child_symbol = ts_node_symbol(child);
        const char* child_type = ts_node_type(child);
        
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_element_type_schema: processing child[%d], type='%s', symbol=%d\n", 
                                       i, child_type, child_symbol);
        
        // Skip the tag name identifier and angle brackets
        if (child_symbol == sym_identifier || child_symbol == 51 || child_symbol == 52) continue;  // < and >
        
        // Check if this is a content type (should be content)
        if (child_symbol == sym_content_type) {
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_element_type_schema: found content_type for content\n");
            // Parse the content type as a content constraint
            TypeSchema* content_schema = build_schema_type(parser, child);  // Use existing parser
            if (content_schema) {
                // Allocate new content array
                TypeSchema** new_content_types = (TypeSchema**)pool_calloc(parser->pool, (content_count + 1) * sizeof(TypeSchema*));
                // Copy old content if any
                if (content_types) {
                    memcpy(new_content_types, content_types, content_count * sizeof(TypeSchema*));
                }
                content_types = new_content_types;
                content_types[content_count] = content_schema;
                content_count++;
                if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_element_type_schema: added content_type, count now %d\n", content_count);
            }
            continue;
        }
        
        // Check if this is a string literal (should be content)
        if (child_symbol == sym_string || strcmp(child_type, "string") == 0) {
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_element_type_schema: found string literal for content\n");
            // Parse the string literal as a content constraint
            TypeSchema* literal_schema = build_schema_type(parser, child);  // Use existing parser
            if (literal_schema) {
                // Allocate new content array
                TypeSchema** new_content_types = (TypeSchema**)pool_calloc(parser->pool, (content_count + 1) * sizeof(TypeSchema*));
                // Copy old content if any
                if (content_types) {
                    memcpy(new_content_types, content_types, content_count * sizeof(TypeSchema*));
                }
                content_types = new_content_types;
                content_types[content_count] = literal_schema;
                content_count++;
                if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_element_type_schema: added content literal, count now %d\n", content_count);
            }
            continue;
        }
        
        // Check if this is a direct type reference for content (like RecipeType+, StepType, etc.)
        if (child_symbol == sym_type_occurrence || child_symbol == sym_primary_type || 
            (child_symbol == sym_identifier && strcmp(child_type, "identifier") == 0)) {
            if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_element_type_schema: found direct type reference for content: symbol=%d, type='%s'\n", child_symbol, child_type);
            // Parse the direct type reference as content
            TypeSchema* type_schema = build_schema_type(parser, child);
            if (type_schema) {
                // Allocate new content array
                TypeSchema** new_content_types = (TypeSchema**)pool_calloc(parser->pool, (content_count + 1) * sizeof(TypeSchema*));
                // Copy old content if any
                if (content_types) {
                    memcpy(new_content_types, content_types, content_count * sizeof(TypeSchema*));
                }
                content_types = new_content_types;
                content_types[content_count] = type_schema;
                content_count++;
                if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_element_type_schema: added direct type reference, count now %d\n", content_count);
            }
            continue;
        }
        
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
    
    // Set the parsed content types
    if (content_count > 0) {
        element_data->content_types = content_types;
        element_data->content_count = content_count;
        if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_element_type_schema: set content_count=%d\n", content_count);
    }
    
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_element_type_schema: finished, content parsing implemented\n");
    if (ENABLE_SCHEMA_DEBUG) printf("[SCHEMA_DEBUG] build_element_type_schema: element_data->content_count=%d\n", element_data->content_count);
    
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
        
        // Look for type_stam and entity_type nodes (which contain type definitions)
        if (child_symbol == 65535) { // ERROR node - try to extract type definition
            uint32_t start_byte = ts_node_start_byte(child);
            uint32_t end_byte = ts_node_end_byte(child);
            if (end_byte > start_byte && end_byte - start_byte < 500) {
                // Try to extract type definition from ERROR node
                const char* error_text = parser->current_source + start_byte;
                int error_len = (int)(end_byte - start_byte);
                
                // Look for pattern: "TypeName = <element"
                char* equals_pos = (char*)memchr(error_text, '=', error_len);
                char* bracket_pos = (char*)memchr(error_text, '<', error_len);
                
                if (equals_pos && bracket_pos && equals_pos < bracket_pos) {
                    // Extract type name (everything before '=')
                    int name_len = equals_pos - error_text;
                    while (name_len > 0 && isspace(error_text[name_len - 1])) name_len--; // trim trailing spaces
                    
                    if (name_len > 0) {
                        // Create a synthetic TypeDefinition for this ERROR node type
                        TypeDefinition* def = (TypeDefinition*)pool_calloc(parser->pool, sizeof(TypeDefinition));
                        if (def) {
                            def->name = (StrView){.str = error_text, .length = (size_t)name_len};
                            // Parse the element definition from the ERROR node text
                            // Look for pattern: "TypeName = <elementName ..."
                            char* element_start = bracket_pos + 1; // Skip '<'
                            char* element_end = (char*)memchr(element_start, '>', error_len - (element_start - error_text));
                            
                            if (element_end) {
                                // Extract element name (first word after '<')
                                char* element_name_end = element_start;
                                while (element_name_end < element_end && !isspace(*element_name_end) && *element_name_end != '>') {
                                    element_name_end++;
                                }
                                
                                int element_name_len = element_name_end - element_start;
                                if (element_name_len > 0) {
                                    // Create element schema with the correct tag name
                                    char* tag_name = (char*)pool_calloc(parser->pool, element_name_len + 1);
                                    memcpy(tag_name, element_start, element_name_len);
                                    tag_name[element_name_len] = '\0';
                                    
                                    def->schema_type = create_element_schema(tag_name, parser->pool);
                                } else {
                                    def->schema_type = build_element_schema(parser, child);
                                }
                            } else {
                                def->schema_type = build_element_schema(parser, child);
                            }
                            
                            if (def->schema_type) {
                                // Check for duplicates and add to type definitions
                                bool is_duplicate = false;
                                for (int j = 0; j < parser->type_definitions->length; j++) {
                                    TypeDefinition* existing_def = (TypeDefinition*)parser->type_definitions->data[j];
                                    if (existing_def && existing_def->name.length == def->name.length &&
                                        memcmp(existing_def->name.str, def->name.str, def->name.length) == 0) {
                                        is_duplicate = true;
                                        break;
                                    }
                                }
                                
                                if (!is_duplicate) {
                                    arraylist_append(parser->type_definitions, def);
                                }
                            }
                        }
                    }
                }
            }
        }
        if (child_symbol == sym_type_stam || child_symbol == sym_entity_type) {
            printf("[SCHEMA_PARSER] DEBUG: Processing %s node\n", child_symbol == sym_type_stam ? "type_stam" : "entity_type");
            TypeDefinition* def = build_type_definition(parser, child);
            if (def) {
                printf("[SCHEMA_PARSER] DEBUG: Built type definition: %.*s\n", (int)def->name.length, def->name.str);
                // Check for duplicate type names before adding
                bool is_duplicate = false;
                for (int j = 0; j < parser->type_definitions->length; j++) {
                    TypeDefinition* existing_def = (TypeDefinition*)parser->type_definitions->data[j];
                    if (existing_def && existing_def->name.length == def->name.length &&
                        memcmp(existing_def->name.str, def->name.str, def->name.length) == 0) {
                        // Found duplicate - log warning and skip
                        fprintf(stderr, "[SCHEMA_PARSER] WARNING: Duplicate type definition '%.*s' found, using first definition\n", 
                               (int)def->name.length, def->name.str);
                        is_duplicate = true;
                        break;
                    }
                }
                
                if (!is_duplicate) {
                    arraylist_append(parser->type_definitions, def);
                }
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
        
        // Look for type_stam and entity_type nodes (which contain type definitions)
        TSSymbol child_symbol = ts_node_symbol(child);
        if (child_symbol == sym_type_stam || child_symbol == sym_entity_type) {
            TypeDefinition* def = build_type_definition(parser, child);
            if (def) {
                // Check for duplicate type names before adding
                bool is_duplicate = false;
                for (int j = 0; j < parser->type_definitions->length; j++) {
                    TypeDefinition* existing_def = (TypeDefinition*)parser->type_definitions->data[j];
                    if (existing_def && existing_def->name.length == def->name.length &&
                        memcmp(existing_def->name.str, def->name.str, def->name.length) == 0) {
                        // Found duplicate - log warning and skip
                        fprintf(stderr, "[SCHEMA_PARSER] WARNING: Duplicate type definition '%.*s' found, using first definition\n", 
                               (int)def->name.length, def->name.str);
                        is_duplicate = true;
                        break;
                    }
                }
                
                if (!is_duplicate) {
                    arraylist_append(parser->type_definitions, def);
                }
            }
        }
        
        // Continue recursively
        parse_all_type_definitions_recursive(parser, child);
    }
}

TypeSchema* find_type_definition(SchemaParser* parser, const char* type_name) {
    if (!parser || !type_name || !parser->type_definitions) return NULL;
    
    size_t name_len = strlen(type_name);
    if (name_len == 0) return NULL;
    
    // Add bounds checking
    if (parser->type_definitions->length < 0 || parser->type_definitions->length > 10000) {
        fprintf(stderr, "[SCHEMA_PARSER] ERROR: Invalid type_definitions length: %d\n", (int)parser->type_definitions->length);
        return NULL;
    }
    
    for (long i = 0; i < parser->type_definitions->length; i++) {
        TypeDefinition* def = (TypeDefinition*)parser->type_definitions->data[i];
        if (!def) continue;
        
        // Add safety checks for the definition
        if (def->name.str == NULL || def->name.length == 0 || def->name.length > 1000) {
            continue;
        }
        
        if (def->name.length == name_len && 
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
