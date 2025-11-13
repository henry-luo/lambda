#include <cstring>
#include <cstdio>
#include <climits>
#include <cstdint>
#include "../lib/hashmap.h"
#include "../lib/mempool.h"
#include "../lib/log.h"
#include "../lib/arraylist.h"
#include "validator.hpp"
#include "transpiler.hpp"
#include "ast.hpp"
#include "lambda-data.hpp"
#include "name_pool.h"
#include "schema_ast.hpp"

// External function declarations
extern "C" {
    TSParser* lambda_parser(void);
    TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
}

// C++ function declarations (no extern "C" needed)
void find_errors(TSNode node);
void print_tree(TSNode node, int depth);
AstNode* build_script(Transpiler* tp, TSNode script_node);
ArrayList* arraylist_new(size_t initial_capacity);

// C++ function declarations (no extern "C" needed)
Transpiler* transpiler_create(Pool* pool);
void transpiler_destroy(Transpiler* transpiler);
AstNode* transpiler_build_ast(Transpiler* transpiler, const char* source);

// Helper to print tree structure for debugging
void print_tree(TSNode node, int depth) {
    if (ts_node_is_null(node)) return;
    
    const char *node_type = ts_node_type(node);
    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);
    
    for (int i = 0; i < depth; ++i) printf("  ");
    printf("%s [%u:%u-%u:%u] %s%s\n", 
           node_type,
           start.row + 1, start.column + 1,
           end.row + 1, end.column + 1,
           ts_node_is_error(node) ? " ERROR" : "",
           ts_node_is_missing(node) ? " MISSING" : "");
    
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        print_tree(ts_node_child(node, i), depth + 1);
    }
}

// Forward declarations for functions we need to implement
Transpiler* transpiler_create(Pool* pool) {
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
        transpiler->ast_pool = pool_create();
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
        log_error("Syntax tree has errors - parsing failed");
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

static uint64_t type_entry_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const TypeRegistryEntry* entry = (const TypeRegistryEntry*)item;
    return hashmap_sip(entry->name_key.str, entry->name_key.length, seed0, seed1);
}

static int type_entry_compare(const void *a, const void *b, void *udata) {
    (void)udata;
    const TypeRegistryEntry* entry_a = (const TypeRegistryEntry*)a;
    const TypeRegistryEntry* entry_b = (const TypeRegistryEntry*)b;

    if (entry_a->name_key.length != entry_b->name_key.length) {
        return (int)entry_a->name_key.length - (int)entry_b->name_key.length;
    }
    return memcmp(entry_a->name_key.str, entry_b->name_key.str, entry_a->name_key.length);
}

// Hash functions for circular reference detection
static uint64_t visited_entry_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const VisitedEntry* entry = (const VisitedEntry*)item;
    return hashmap_sip(entry->key.str, entry->key.length, seed0, seed1);
}

static int visited_entry_compare(const void *a, const void *b, void *udata) {
    (void)udata;
    const VisitedEntry* entry_a = (const VisitedEntry*)a;
    const VisitedEntry* entry_b = (const VisitedEntry*)b;

    if (entry_a->key.length != entry_b->key.length) {
        return (int)entry_a->key.length - (int)entry_b->key.length;
    }
    return memcmp(entry_a->key.str, entry_b->key.str, entry_a->key.length);
}

// ==================== Core Validator Functions ====================

AstValidator* ast_validator_create(Pool* pool) {
    if (!pool) return nullptr;

    AstValidator* validator = (AstValidator*)pool_calloc(pool, sizeof(AstValidator));
    if (!validator) return nullptr;

    validator->pool = pool;

    validator->transpiler = transpiler_create(pool);
    if (!validator->transpiler) {
        return nullptr;
    }

    // Initialize type definitions registry
    validator->type_definitions = hashmap_new(sizeof(TypeRegistryEntry), 0, 0, 0,
                                   type_entry_hash, type_entry_compare, NULL, pool);
    if (!validator->type_definitions) {
        return nullptr;
    }

    // Initialize visited nodes for circular reference detection
    validator->visited_nodes = hashmap_new(sizeof(VisitedEntry), 0, 0, 0,
                                   visited_entry_hash, visited_entry_compare, NULL, pool);
    if (!validator->visited_nodes) {
        return nullptr;
    }

    // Initialize default validation options
    validator->options.strict_mode = false;
    validator->options.allow_unknown_fields = true;
    validator->options.allow_empty_elements = true;
    validator->options.max_depth = 1024;

    return validator;
}

void ast_validator_destroy(AstValidator* validator) {
    if (!validator) return;

    if (validator->type_definitions) {
        hashmap_free(validator->type_definitions);
    }

    if (validator->visited_nodes) {
        hashmap_free(validator->visited_nodes);
    }

    if (validator->transpiler) {
        transpiler_destroy(validator->transpiler);
    }

    // Note: Memory pool cleanup handled by caller
}

int ast_validator_load_schema(AstValidator* validator, const char* source, const char* root_type) {
    if (!validator || !source || !root_type) return -1;

    log_info("Loading schema with root type: %s", root_type);

    // Build AST using transpiler
    AstNode* ast = transpiler_build_ast(validator->transpiler, source);
    if (!ast) {
        log_error("Failed to build AST from source");
        return -1;
    }

    log_debug("AST built successfully, extracting type definitions");

    // Extract type definitions from AST
    if (ast->node_type == AST_SCRIPT) {
        AstScript* script = (AstScript*)ast;
        AstNode* child = script->child;
        
        // If the script child is a content node, traverse into it
        if (child && child->node_type == AST_NODE_CONTENT) {
            AstListNode* content = (AstListNode*)child;
            child = content->item;
        }
        
        int type_count = 0;
        while (child) {
            if (child->node_type == AST_NODE_TYPE_STAM) {
                // Type statement: type Name = TypeExpr
                AstNamedNode* type_node = (AstNamedNode*)child;
                
                if (!type_node->name || !type_node->type) {
                    log_warn("Skipping type node without name or type");
                    child = child->next;
                    continue;
                }
                
                // Create TypeDefinition
                TypeDefinition* def = (TypeDefinition*)pool_calloc(validator->pool, sizeof(TypeDefinition));
                if (!def) {
                    log_error("Failed to allocate TypeDefinition");
                    return -1;
                }
                
                def->name.str = type_node->name->chars;
                def->name.length = type_node->name->len;
                def->runtime_type = type_node->type;
                def->schema_type = nullptr;  // Can be filled in later if needed
                def->is_exported = true;  // Assume exported by default
                
                // Create TypeRegistryEntry for hashmap
                TypeRegistryEntry entry;
                entry.definition = def;
                entry.name_key = def->name;
                
                hashmap_set(validator->type_definitions, &entry);
                
                log_debug("Registered type: %.*s (type_id=%d)",
                    (int)def->name.length, def->name.str,
                    type_node->type ? type_node->type->type_id : -1);
                type_count++;
            }
            child = child->next;
        }
        
        log_info("Registered %d type definitions", type_count);
        return 0;
    }

    log_error("AST root is not a script node");
    return -1;
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
    TypeRegistryEntry key = {.definition = nullptr, .name_key = name_view};

    const TypeRegistryEntry* entry = (const TypeRegistryEntry*)hashmap_get(validator->type_definitions, &key);
    return entry && entry->definition ? entry->definition->runtime_type : nullptr;
}

// Resolve a type reference with circular reference detection
// Returns the resolved Type* or nullptr if not found or circular
Type* ast_validator_resolve_type_reference(AstValidator* validator, const char* type_name) {
    if (!validator || !type_name) return nullptr;

    StrView name_view = {.str = type_name, .length = strlen(type_name)};

    // Check if we're already visiting this type (circular reference)
    VisitedEntry visited_key = {.key = name_view, .visited = false};
    const VisitedEntry* existing = (const VisitedEntry*)hashmap_get(validator->visited_nodes, &visited_key);
    
    if (existing && existing->visited) {
        log_error("[AST_VALIDATOR] Circular type reference detected: %.*s", 
                  (int)name_view.length, name_view.str);
        return nullptr;
    }

    // Mark this type as being visited
    VisitedEntry visit_entry = {.key = name_view, .visited = true};
    hashmap_set(validator->visited_nodes, &visit_entry);

    // Look up the type
    Type* resolved_type = ast_validator_find_type(validator, type_name);

    // Unmark after resolution (allow revisiting in different validation paths)
    VisitedEntry unvisit_entry = {.key = name_view, .visited = false};
    hashmap_set(validator->visited_nodes, &unvisit_entry);

    return resolved_type;
}

// ==================== Error Handling ====================

ValidationResult* create_validation_result(Pool* pool) {
    ValidationResult* result;

    if (pool) {
        result = (ValidationResult*)pool_calloc(pool, sizeof(ValidationResult));
    } else {
        // For error cases with null pool, use malloc
        result = (ValidationResult*)calloc(1, sizeof(ValidationResult));
    }
    if (!result) return nullptr;

    result->valid = true;
    result->error_count = 0;
    result->errors = nullptr;

    return result;
}

ValidationError* create_validation_error(ValidationErrorCode code, const char* message, PathSegment* path, Pool* pool) {
    if (!message) return nullptr;

    ValidationError* error;
    if (pool) {
        error = (ValidationError*)pool_calloc(pool, sizeof(ValidationError));
    } else {
        // For error cases with null pool, use malloc
        error = (ValidationError*)calloc(1, sizeof(ValidationError));
    }
    if (!error) return nullptr;

    error->code = code;

    // Copy message
    if (message) {
        size_t msg_len = strlen(message) + 1;
        if (pool) {
            error->message = create_string(pool, message);
        } else {
            // For non-pool allocation, create a simple string copy
            size_t len = strlen(message);
            error->message = (String*)malloc(sizeof(String) + len + 1);
            if (error->message) {
                error->message->len = len;
                error->message->ref_cnt = 1;
                strcpy(error->message->chars, message);
            }
        }
    }

    // Set path
    error->path = path;

    error->expected = nullptr;
    error->actual = (Item){0};
    error->next = nullptr;

    return error;
}

void add_validation_error(ValidationResult* result, ValidationError* error) {
    if (!result || !error) return;

    result->valid = false;
    result->error_count++;

    // Add to linked list
    if (!result->errors) {
        result->errors = error;
    } else {
        ValidationError* current = result->errors;
        while (current->next) {
            current = current->next;
        }
        current->next = error;
    }
}

void free_ast_validation_result(ValidationResult* result) {
    // Memory cleanup handled by pool
    (void)result;
}

bool is_item_compatible_with_type(ConstItem item, Type* type) {
    if (!type) return false;

    // For Phase 1, simple type ID comparison
    return item.type_id() == type->type_id;
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

void merge_validation_results(ValidationResult* dest, ValidationResult* src) {
    if (!dest || !src) return;

    // If source has errors, merge them into destination
    if (src->errors) {
        dest->valid = false;
        dest->error_count += src->error_count;

        // Add all source errors to destination
        ValidationError* src_error = src->errors;
        while (src_error) {
            ValidationError* copied_error = create_validation_error(
                src_error->code,
                src_error->message ? src_error->message->chars : "Unknown error",
                src_error->path,
                nullptr);
            if (copied_error) {
                add_validation_error(dest, copied_error);
            }
            src_error = src_error->next;
        }
    }

    // Merge warnings if present
    if (src->warnings) {
        dest->warning_count += src->warning_count;
        // Note: Warning merging implementation would go here
    }
}

// ==================== Validation Functions ====================

// validate 'item' against type 'type'
ValidationResult* ast_validator_validate_type(AstValidator* validator, ConstItem item, Type* type) {
    if (!validator || !type) {
        ValidationResult* result = create_validation_result(validator ? validator->pool : nullptr);
        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Invalid validator or type",
            nullptr, validator ? validator->pool : nullptr);
        add_validation_error(result, error);
        return result;
    }
    // init validation context
    validator->current_path = nullptr;
    validator->current_depth = 0;
    return validate_against_type(validator, item, type);
}

// validate 'item' against type named 'type_name'
ValidationResult* ast_validator_validate(AstValidator* validator, ConstItem item, const char* type_name) {
    if (!validator || !type_name) {
        ValidationResult* result = create_validation_result(validator ? validator->pool : nullptr);
        if (result) {
            add_validation_error(result, create_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, "Invalid validator or type name",
                nullptr, validator ? validator->pool : nullptr));
        }
        return result;
    }

    // Use resolve_type_reference for circular reference detection
    Type* type = ast_validator_resolve_type_reference(validator, type_name);
    if (!type) {
        ValidationResult* result = create_validation_result(validator->pool);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type not found or circular reference detected: %s", type_name);
        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_REFERENCE_ERROR, error_msg, nullptr, validator->pool);
        add_validation_error(result, error);
        return result;
    }

    return ast_validator_validate_type(validator, item, type);
}
