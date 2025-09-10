/**
 * @file validator.cpp
 * @brief New AST-Based Lambda Validator - Implementation
 * @author Henry Luo
 * @license MIT
 */

#include "validator.h"
#include "transpiler.hpp"
#include "ast.hpp"
#include "lambda-data.hpp"
#include "../lib/hashmap.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include "../lib/log.h"
#include "name_pool.h"
#include "../lib/arraylist.h"
#include <cstring>
#include <cstdio>
#include <climits>

// External function declarations
extern "C" {
    TSParser* lambda_parser(void);
    TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
    void find_errors(TSNode node);
}

// Forward declarations from build_ast.cpp
AstNode* build_script(Transpiler* tp, TSNode script_node);
ArrayList* arraylist_new(size_t initial_capacity);

// External function declarations
extern "C" {
    Transpiler* transpiler_create(VariableMemPool* pool);
    void transpiler_destroy(Transpiler* transpiler);
    AstNode* transpiler_build_ast(Transpiler* transpiler, const char* source);
}

// Forward declarations for functions we need to implement
Transpiler* transpiler_create(VariableMemPool* pool) {
    // Placeholder implementation for Phase 1
    Transpiler* tp = (Transpiler*)pool_calloc(pool, sizeof(Transpiler));
    if (tp) {
        tp->ast_pool = pool;
    }
    return tp;
}

void transpiler_destroy(Transpiler* transpiler) {
    // Placeholder - memory handled by pool
    (void)transpiler;
}

AstNode* transpiler_build_ast(Transpiler* transpiler, const char* source) {
    if (!transpiler || !source) {
        return nullptr;
    }
    
    // Initialize transpiler components if not already done
    if (!transpiler->parser) {
        transpiler->parser = lambda_parser();
        if (!transpiler->parser) {
            return nullptr;
        }
    }
    
    // Initialize memory pool if not already done
    if (!transpiler->ast_pool) {
        size_t grow_size = 4096;  // 4k
        size_t tolerance_percent = 20;
        pool_variable_init(&transpiler->ast_pool, grow_size, tolerance_percent);
    }
    
    // Initialize type and const lists if not already done
    if (!transpiler->type_list) {
        transpiler->type_list = arraylist_new(16);
    }
    if (!transpiler->const_list) {
        transpiler->const_list = arraylist_new(16);
    }
    
    // Initialize name pool if not already done
    if (!transpiler->name_pool) {
        transpiler->name_pool = name_pool_create(transpiler->ast_pool, nullptr);
        if (!transpiler->name_pool) {
            return nullptr;
        }
    }
    
    // Parse the source code to syntax tree
    transpiler->source = source;
    transpiler->syntax_tree = lambda_parse_source(transpiler->parser, source);
    if (!transpiler->syntax_tree) {
        return nullptr;
    }
    
    // Get root node and validate
    TSNode root_node = ts_tree_root_node(transpiler->syntax_tree);
    if (ts_node_has_error(root_node)) {
        // Log syntax errors but continue - validator will handle them
        find_errors(root_node);
        return nullptr;
    }
    
    // Validate root node type
    if (strcmp(ts_node_type(root_node), "document") != 0) {
        return nullptr;
    }
    
    // Build AST from syntax tree using existing build_script function
    AstNode* ast_root = build_script(transpiler, root_node);
    transpiler->ast_root = ast_root;
    
    return ast_root;
}

// ==================== Hash Functions for Type Registry ====================

typedef struct TypeRegistryEntry {
    StrView name;
    Type* type;
} TypeRegistryEntry;

static uint64_t type_entry_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const TypeRegistryEntry* entry = (const TypeRegistryEntry*)item;
    return hashmap_sip(entry->name.str, entry->name.length, seed0, seed1);
}

static int type_entry_compare(const void *a, const void *b, void *udata) {
    (void)udata;
    const TypeRegistryEntry* entry_a = (const TypeRegistryEntry*)a;
    const TypeRegistryEntry* entry_b = (const TypeRegistryEntry*)b;
    
    if (entry_a->name.length != entry_b->name.length) {
        return (int)entry_a->name.length - (int)entry_b->name.length;
    }
    return memcmp(entry_a->name.str, entry_b->name.str, entry_a->name.length);
}

// ==================== Core Validator Functions ====================

AstValidator* ast_validator_create(VariableMemPool* pool) {
    if (!pool) return nullptr;
    
    AstValidator* validator = (AstValidator*)pool_calloc(pool, sizeof(AstValidator));
    if (!validator) return nullptr;
    
    // Initialize memory pool
    validator->pool = pool;
    
    // Create transpiler for AST building
    validator->transpiler = transpiler_create(pool);
    if (!validator->transpiler) {
        return nullptr;
    }
    
    // Create type definitions registry
    validator->type_definitions = hashmap_new(
        sizeof(TypeRegistryEntry), 32, 0, 0,
        type_entry_hash, type_entry_compare, nullptr, nullptr
    );
    
    if (!validator->type_definitions) {
        return nullptr;
    }
    
    // Initialize default options
    validator->default_options.strict_mode = false;
    validator->default_options.allow_unknown_fields = true;
    validator->default_options.allow_empty_elements = false;
    validator->default_options.max_depth = 100;
    validator->default_options.timeout_ms = 0;
    
    return validator;
}

void ast_validator_destroy(AstValidator* validator) {
    if (!validator) return;
    
    if (validator->type_definitions) {
        hashmap_free(validator->type_definitions);
    }
    
    if (validator->transpiler) {
        transpiler_destroy(validator->transpiler);
    }
    
    // Note: Memory pool cleanup handled by caller
}

int ast_validator_load_schema(AstValidator* validator, const char* source, const char* root_type) {
    if (!validator || !source || !root_type) return -1;
    
    printf("[AST_VALIDATOR] Loading schema with root type: %s\n", root_type);
    
    // Build AST using transpiler
    AstNode* ast = transpiler_build_ast(validator->transpiler, source);
    if (!ast) {
        printf("[AST_VALIDATOR] Failed to build AST from source\n");
        return -1;
    }
    
    printf("[AST_VALIDATOR] AST built successfully, extracting type definitions\n");
    
    // Extract type definitions from AST
    // For Phase 1, we'll implement a simple type extraction
    // TODO: Implement full AST traversal for type definitions
    
    return 0;
}

// ==================== Type Extraction ====================

Type* extract_type_from_ast_node(AstNode* node) {
    if (!node) return nullptr;
    
    // For Phase 1, handle basic type extraction
    // This will be expanded in later phases
    switch (node->node_type) {
        case AST_NODE_TYPE_STAM: {
            // Type statement node - extract the type definition
            AstNamedNode* type_node = (AstNamedNode*)node;
            return type_node->type;
        }
        default:
            return nullptr;
    }
}

Type* ast_validator_find_type(AstValidator* validator, const char* type_name) {
    if (!validator || !type_name) return nullptr;
    
    StrView name_view = {.str = type_name, .length = strlen(type_name)};
    TypeRegistryEntry key = {.name = name_view, .type = nullptr};
    
    const TypeRegistryEntry* entry = (const TypeRegistryEntry*)hashmap_get(validator->type_definitions, &key);
    return entry ? entry->type : nullptr;
}

// ==================== Validation Functions ====================

AstValidationResult* ast_validator_validate(AstValidator* validator, TypedItem item, const char* type_name) {
    if (!validator || !type_name) {
        AstValidationResult* result = create_ast_validation_result(validator ? validator->pool : nullptr);
        if (result) {
            add_ast_validation_error(result, create_ast_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, "Invalid validator or type name", 
                "", validator ? validator->pool : nullptr));
        }
        return result;
    }
    
    Type* type = ast_validator_find_type(validator, type_name);
    if (!type) {
        AstValidationResult* result = create_ast_validation_result(validator->pool);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Type not found: %s", type_name);
        add_ast_validation_error(result, create_ast_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, error_msg, "", validator->pool));
        return result;
    }
    
    return ast_validator_validate_type(validator, item, type);
}

AstValidationResult* ast_validator_validate_type(AstValidator* validator, TypedItem item, Type* type) {
    if (!validator || !type) {
        AstValidationResult* result = create_ast_validation_result(validator ? validator->pool : nullptr);
        AstValidationError* error = create_ast_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Invalid validator or type", 
            "", validator ? validator->pool : nullptr);
        add_ast_validation_error(result, error);
        return result;
    }
    
    // Create validation context
    AstValidationContext ctx = {};
    ctx.validator = validator;
    ctx.current_path = (char*)"";
    ctx.current_depth = 0;
    ctx.options = validator->default_options;
    ctx.pool = validator->pool;
    
    return validate_against_type(validator, item, type, &ctx);
}

AstValidationResult* validate_against_type(AstValidator* validator, TypedItem item, Type* type, AstValidationContext* ctx) {
    if (!validator || !type || !ctx) {
        AstValidationResult* result = create_ast_validation_result(ctx ? ctx->pool : nullptr);
        if (result) {
            add_ast_validation_error(result, create_ast_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, "Invalid validation parameters", 
                ctx ? ctx->current_path : "", ctx ? ctx->pool : nullptr));
        }
        return result;
    }
    
    // Check validation depth
    if (ctx->current_depth >= ctx->options.max_depth) {
        AstValidationResult* result = create_ast_validation_result(ctx->pool);
        add_ast_validation_error(result, create_ast_validation_error(
            AST_VALID_ERROR_CONSTRAINT_VIOLATION, "Maximum validation depth exceeded", 
            ctx->current_path, ctx->pool));
        return result;
    }
    
    ctx->current_depth++;
    
    AstValidationResult* result = nullptr;
    
    // Dispatch based on type
    printf("[AST_VALIDATOR] Validating against type_id: %d\n", type->type_id);
    
    switch (type->type_id) {
        case LMD_TYPE_STRING:
        case LMD_TYPE_INT:
        case LMD_TYPE_FLOAT:
        case LMD_TYPE_BOOL:
        case LMD_TYPE_NULL:
            result = validate_against_primitive_type(item, type, ctx);
            break;
            
        case LMD_TYPE_ARRAY:
        case LMD_TYPE_LIST:
            result = validate_against_array_type(validator, item, (TypeArray*)type, ctx);
            break;
            
        case LMD_TYPE_MAP:
            return validate_against_map_type(validator, item, (TypeMap*)type, ctx);
        
        case LMD_TYPE_ELEMENT:
            return validate_against_element_type(validator, item, (TypeElmt*)type, ctx);
        
        default:
            result = create_ast_validation_result(ctx->pool);
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Unsupported type for validation: %d", type->type_id);
            add_ast_validation_error(result, create_ast_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, error_msg, ctx->current_path, ctx->pool));
            break;
    }
    
    ctx->current_depth--;
    return result;
}

AstValidationResult* validate_against_primitive_type(TypedItem item, Type* type, AstValidationContext* ctx) {
    printf("[AST_VALIDATOR] Validating primitive: expected=%d, actual=%d\n", type->type_id, item.type_id);
    
    AstValidationResult* result = create_ast_validation_result(ctx->pool);
    
    if (!is_item_compatible_with_type(item, type)) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: expected %s, got type %d",
                type_to_string(type), item.type_id);
        
        AstValidationError* error = create_ast_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->current_path, ctx->pool);
        if (error) {
            error->expected_type = type;
            error->actual_item = item;
            add_ast_validation_error(result, error);
        }
    }
    
    return result;
}

// ==================== Error Handling ====================

AstValidationResult* create_ast_validation_result(VariableMemPool* pool) {
    AstValidationResult* result;
    
    if (pool) {
        result = (AstValidationResult*)pool_calloc(pool, sizeof(AstValidationResult));
    } else {
        // For error cases with null pool, use malloc
        result = (AstValidationResult*)calloc(1, sizeof(AstValidationResult));
    }
    if (!result) return nullptr;
    
    result->valid = true;
    result->error_count = 0;
    result->errors = nullptr;
    result->pool = pool;
    
    return result;
}

AstValidationError* create_ast_validation_error(AstValidationErrorType type, const char* message, 
                                               const char* path, VariableMemPool* pool) {
    if (!message) return nullptr;
    
    AstValidationError* error;
    if (pool) {
        error = (AstValidationError*)pool_calloc(pool, sizeof(AstValidationError));
    } else {
        // For error cases with null pool, use malloc
        error = (AstValidationError*)calloc(1, sizeof(AstValidationError));
    }
    if (!error) return nullptr;
    
    error->error_type = type;
    
    // Copy message
    if (message) {
        size_t msg_len = strlen(message) + 1;
        if (pool) {
            error->message = (char*)pool_calloc(pool, msg_len);
        } else {
            error->message = (char*)malloc(msg_len);
        }
        if (error->message) {
            strcpy(error->message, message);
        }
    }
    
    // Copy path
    if (path) {
        size_t path_len = strlen(path) + 1;
        if (pool) {
            error->path = (char*)pool_calloc(pool, path_len);
        } else {
            error->path = (char*)malloc(path_len);
        }
        if (error->path) {
            strcpy(error->path, path);
        }
    }
    
    error->expected_type = nullptr;
    error->actual_item = {};
    error->next = nullptr;
    
    return error;
}

void add_ast_validation_error(AstValidationResult* result, AstValidationError* error) {
    if (!result || !error) return;
    
    result->valid = false;
    result->error_count++;
    
    // Add to linked list
    if (!result->errors) {
        result->errors = error;
    } else {
        AstValidationError* current = result->errors;
        while (current->next) {
            current = current->next;
        }
        current->next = error;
    }
}

void free_ast_validation_result(AstValidationResult* result) {
    // Memory cleanup handled by pool
    (void)result;
}

// ==================== Utility Functions ====================

bool is_item_compatible_with_type(TypedItem item, Type* type) {
    if (!type) return false;
    
    // For Phase 1, simple type ID comparison
    return item.type_id == type->type_id;
}

const char* type_to_string(Type* type) {
    if (!type) return "unknown";
    
    switch (type->type_id) {
        case LMD_TYPE_STRING: return "string";
        case LMD_TYPE_INT: return "int";
        case LMD_TYPE_FLOAT: return "float";
        case LMD_TYPE_BOOL: return "bool";
        case LMD_TYPE_NULL: return "null";
        case LMD_TYPE_ARRAY: return "array";
        case LMD_TYPE_MAP: return "map";
        case LMD_TYPE_ELEMENT: return "element";
        default: return "unknown";
    }
}

// ==================== Phase 2: Array and Map Validation ====================

AstValidationResult* validate_against_array_type(AstValidator* validator, TypedItem item, TypeArray* array_type, AstValidationContext* ctx) {
    AstValidationResult* result = create_ast_validation_result(ctx->pool);
    
    // Check if item is actually an array/list
    if (item.type_id != LMD_TYPE_ARRAY && item.type_id != LMD_TYPE_LIST) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: expected array, got type %d", item.type_id);
        add_ast_validation_error(result, create_ast_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->current_path, ctx->pool));
        return result;
    }
    
    // Get the actual array data
    List* array_data = (List*)item.pointer;
    if (!array_data) {
        add_ast_validation_error(result, create_ast_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Array data is null", ctx->current_path, ctx->pool));
        return result;
    }
    
    // Validate array length if specified
    if (array_type->length >= 0 && array_data->length != array_type->length) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Array length mismatch: expected %ld, got %ld", 
                array_type->length, array_data->length);
        add_ast_validation_error(result, create_ast_validation_error(
            AST_VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, ctx->current_path, ctx->pool));
    }
    
    // Validate each array element against nested type
    if (array_type->nested && array_data->length > 0) {
        for (long i = 0; i < array_data->length; i++) {
            // Create path for array element
            char element_path[512];
            snprintf(element_path, sizeof(element_path), "%s[%ld]", ctx->current_path, i);
            
            // Create context for element validation
            AstValidationContext element_ctx = *ctx;
            element_ctx.current_path = element_path;
            
            // Get array element (convert Item to TypedItem)
            Item element = array_data->items[i];
            TypedItem element_item;
            element_item.type_id = (TypeId)element.type_id;
            element_item.pointer = (void*)(uintptr_t)element.pointer;
            
            // Recursively validate element
            AstValidationResult* element_result = validate_against_type(
                validator, element_item, array_type->nested, &element_ctx);
            
            // Merge element validation results
            if (element_result && !element_result->valid) {
                result->valid = false;
                
                // Add all element errors to main result
                AstValidationError* element_error = element_result->errors;
                while (element_error) {
                    result->error_count++;
                    
                    // Copy error to main result
                    AstValidationError* copied_error = create_ast_validation_error(
                        element_error->error_type, element_error->message, 
                        element_error->path, ctx->pool);
                    add_ast_validation_error(result, copied_error);
                    
                    element_error = element_error->next;
                }
            }
        }
    }
    
    return result;
}

AstValidationResult* validate_against_map_type(AstValidator* validator, TypedItem item, TypeMap* map_type, AstValidationContext* ctx) {
    AstValidationResult* result = create_ast_validation_result(ctx->pool);
    
    // Check if item is actually a map
    if (item.type_id != LMD_TYPE_MAP) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: expected map, got type %d", item.type_id);
        add_ast_validation_error(result, create_ast_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->current_path, ctx->pool));
        return result;
    }
    
    // Get the actual map data (assuming it's stored as a struct with typed fields)
    void* map_data = item.pointer;
    if (!map_data) {
        add_ast_validation_error(result, create_ast_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Map data is null", ctx->current_path, ctx->pool));
        return result;
    }
    
    // Validate each field in the map shape
    ShapeEntry* shape_entry = map_type->shape;
    while (shape_entry) {
        // Create path for map field
        char field_path[512];
        snprintf(field_path, sizeof(field_path), "%s.%.*s", 
                ctx->current_path, (int)shape_entry->name->length, shape_entry->name->str);
        
        // Create context for field validation
        AstValidationContext field_ctx = *ctx;
        field_ctx.current_path = field_path;
        
        // Extract field value from map data using byte offset
        void* field_data = (char*)map_data + shape_entry->byte_offset;
        
        // Create TypedItem for the field (this is simplified - would need proper type detection)
        TypedItem field_item;
        field_item.type_id = shape_entry->type->type_id;
        field_item.pointer = field_data;
        
        // Recursively validate field
        AstValidationResult* field_result = validate_against_type(
            validator, field_item, shape_entry->type, &field_ctx);
        
        // Merge field validation results
        if (field_result && !field_result->valid) {
            result->valid = false;
            
            // Add all field errors to main result
            AstValidationError* field_error = field_result->errors;
            while (field_error) {
                result->error_count++;
                
                // Copy error to main result
                AstValidationError* copied_error = create_ast_validation_error(
                    field_error->error_type, field_error->message, 
                    field_error->path, ctx->pool);
                add_ast_validation_error(result, copied_error);
                
                field_error = field_error->next;
            }
        }
        
        shape_entry = shape_entry->next;
    }
    
    return result;
}

// ==================== Phase 3: Element Validation ====================

AstValidationResult* validate_against_element_type(AstValidator* validator, TypedItem item, TypeElmt* element_type, AstValidationContext* ctx) {
    AstValidationResult* result = create_ast_validation_result(ctx->pool);
    
    // Check if item is actually an element
    if (item.type_id != LMD_TYPE_ELEMENT) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: expected element, got type %d", item.type_id);
        add_ast_validation_error(result, create_ast_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->current_path, ctx->pool));
        return result;
    }
    
    // Get the actual element data
    Element* element_data = (Element*)item.pointer;
    if (!element_data) {
        add_ast_validation_error(result, create_ast_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Element data is null", ctx->current_path, ctx->pool));
        return result;
    }
    
    // Validate element name if specified
    if (element_type->name.length > 0) {
        char element_name_error[512];
        snprintf(element_name_error, sizeof(element_name_error), 
                "Element name validation for: %.*s", 
                (int)element_type->name.length, element_type->name.str);
        printf("[AST_VALIDATOR] %s\n", element_name_error);
    }
    
    // Validate element attributes using inherited map validation
    // TypeElmt inherits from TypeMap, so we can validate attributes as map fields
    TypeMap* map_part = (TypeMap*)element_type;
    if (map_part->shape) {
        // Create a TypedItem for the element's attribute data
        TypedItem attr_item;
        attr_item.type_id = LMD_TYPE_MAP;
        attr_item.pointer = element_data->data;
        
        // Create context for attribute validation
        char attr_path[512];
        snprintf(attr_path, sizeof(attr_path), "%s@attributes", ctx->current_path);
        AstValidationContext attr_ctx = *ctx;
        attr_ctx.current_path = attr_path;
        
        // Validate attributes using map validation
        AstValidationResult* attr_result = validate_against_map_type(validator, attr_item, map_part, &attr_ctx);
        
        // Merge attribute validation results
        if (attr_result && !attr_result->valid) {
            result->valid = false;
            result->error_count += attr_result->error_count;
            
            AstValidationError* attr_error = attr_result->errors;
            while (attr_error) {
                AstValidationError* copied_error = create_ast_validation_error(
                    attr_error->error_type, attr_error->message, 
                    attr_error->path, ctx->pool);
                add_ast_validation_error(result, copied_error);
                attr_error = attr_error->next;
            }
        }
    }
    
    // Validate element content length
    if (element_type->content_length > 0) {
        char content_path[512];
        snprintf(content_path, sizeof(content_path), "%s/content", ctx->current_path);
        
        if (element_data->length > element_type->content_length) {
            char content_error[256];
            snprintf(content_error, sizeof(content_error), 
                    "Element content length mismatch: expected %ld, got %ld", 
                    element_type->content_length, element_data->length);
            add_ast_validation_error(result, create_ast_validation_error(
                AST_VALID_ERROR_CONSTRAINT_VIOLATION, content_error, content_path, ctx->pool));
        }
    }
    
    return result;
}

// ==================== Phase 4: Union and Advanced Validation ====================

AstValidationResult* validate_against_union_type(AstValidator* validator, TypedItem item, Type** union_types, int type_count, AstValidationContext* ctx) {
    AstValidationResult* result = create_ast_validation_result(ctx->pool);
    
    if (!union_types || type_count <= 0) {
        add_ast_validation_error(result, create_ast_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Invalid union type definition", ctx->current_path, ctx->pool));
        return result;
    }
    
    // Try to validate against each type in the union
    bool any_valid = false;
    AstValidationResult* best_result = nullptr;
    int min_errors = 2147483647; // INT_MAX value
    
    for (int i = 0; i < type_count; i++) {
        if (!union_types[i]) continue;
        
        // Create context for union member validation
        char union_path[512];
        snprintf(union_path, sizeof(union_path), "%s|type_%d", ctx->current_path, i);
        AstValidationContext union_ctx = *ctx;
        union_ctx.current_path = union_path;
        
        // Try validating against this union member
        AstValidationResult* member_result = validate_against_type(validator, item, union_types[i], &union_ctx);
        
        if (member_result && member_result->valid) {
            any_valid = true;
            result->valid = true;
            return result;
        } else if (member_result && member_result->error_count < min_errors) {
            min_errors = member_result->error_count;
            best_result = member_result;
        }
    }
    
    // If no type in the union was valid, report the best error
    if (!any_valid) {
        result->valid = false;
        
        if (best_result) {
            result->error_count = best_result->error_count;
            AstValidationError* error = best_result->errors;
            while (error) {
                AstValidationError* copied_error = create_ast_validation_error(
                    error->error_type, error->message, error->path, ctx->pool);
                add_ast_validation_error(result, copied_error);
                error = error->next;
            }
        } else {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Item does not match any type in union (%d types)", type_count);
            add_ast_validation_error(result, create_ast_validation_error(
                AST_VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->current_path, ctx->pool));
        }
    }
    
    return result;
}

AstValidationResult* validate_occurrence_constraint(AstValidator* validator, TypedItem* items, long item_count, Type* expected_type, Operator occurrence_op, AstValidationContext* ctx) {
    AstValidationResult* result = create_ast_validation_result(ctx->pool);
    
    if (!items || !expected_type) {
        add_ast_validation_error(result, create_ast_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Invalid occurrence constraint parameters", ctx->current_path, ctx->pool));
        return result;
    }
    
    // Validate occurrence constraints based on operator
    switch (occurrence_op) {
        case OPERATOR_OPTIONAL: // ? (0 or 1)
            if (item_count > 1) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "Optional constraint violated: expected 0 or 1 items, got %ld", item_count);
                add_ast_validation_error(result, create_ast_validation_error(
                    AST_VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, ctx->current_path, ctx->pool));
            }
            break;
            
        case OPERATOR_ONE_MORE: // + (1 or more)
            if (item_count < 1) {
                add_ast_validation_error(result, create_ast_validation_error(
                    AST_VALID_ERROR_CONSTRAINT_VIOLATION, 
                    "One-or-more constraint violated: expected at least 1 item, got 0", 
                    ctx->current_path, ctx->pool));
            }
            break;
            
        case OPERATOR_ZERO_MORE: // * (0 or more)
            // Always valid for zero-or-more
            break;
            
        default:
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Unsupported occurrence operator: %d", occurrence_op);
            add_ast_validation_error(result, create_ast_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, error_msg, ctx->current_path, ctx->pool));
            return result;
    }
    
    // Validate each item against the expected type
    for (long i = 0; i < item_count; i++) {
        char item_path[512];
        snprintf(item_path, sizeof(item_path), "%s[%ld]", ctx->current_path, i);
        AstValidationContext item_ctx = *ctx;
        item_ctx.current_path = item_path;
        item_ctx.current_depth++;
        
        AstValidationResult* item_result = validate_against_type(validator, items[i], expected_type, &item_ctx);
        
        if (item_result && !item_result->valid) {
            result->valid = false;
            result->error_count += item_result->error_count;
            
            AstValidationError* item_error = item_result->errors;
            while (item_error) {
                AstValidationError* copied_error = create_ast_validation_error(
                    item_error->error_type, item_error->message, 
                    item_error->path, ctx->pool);
                add_ast_validation_error(result, copied_error);
                item_error = item_error->next;
            }
        }
    }
    
    return result;
}
