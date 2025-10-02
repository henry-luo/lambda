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

// External function declarations
extern "C" {
    TSParser* lambda_parser(void);
    TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
}

// C++ function declarations (no extern "C" needed)
void find_errors(TSNode node);
AstNode* build_script(Transpiler* tp, TSNode script_node);
ArrayList* arraylist_new(size_t initial_capacity);

// C++ function declarations (no extern "C" needed)
Transpiler* transpiler_create(Pool* pool);
void transpiler_destroy(Transpiler* transpiler);
AstNode* transpiler_build_ast(Transpiler* transpiler, const char* source);

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
ValidationResult* ast_validator_validate_type(AstValidator* validator, TypedItem item, Type* type) {
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
ValidationResult* ast_validator_validate(AstValidator* validator, TypedItem item, const char* type_name) {
    if (!validator || !type_name) {
        ValidationResult* result = create_validation_result(validator ? validator->pool : nullptr);
        if (result) {
            add_validation_error(result, create_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, "Invalid validator or type name",
                nullptr, validator ? validator->pool : nullptr));
        }
        return result;
    }

    Type* type = ast_validator_find_type(validator, type_name);
    if (!type) {
        ValidationResult* result = create_validation_result(validator->pool);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Type not found: %s", type_name);
        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, error_msg, nullptr, validator->pool);
        add_validation_error(result, error);
        return result;
    }

    return ast_validator_validate_type(validator, item, type);
}
