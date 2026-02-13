#include <cstring>
#include <cstdio>
#include <climits>
#include <cstdint>
#include "../../lib/hashmap.h"
#include "../../lib/mempool.h"
#include "../../lib/log.h"
#include "../../lib/arraylist.h"
#include "../../lib/str.h"
#include "../transpiler.hpp"
#include "validator.hpp"
#include "../schema_ast.hpp"
#include "../mark_reader.hpp"

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
        tp->pool = pool;
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
    if (!transpiler->pool) {
        transpiler->pool = pool_create();
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
        transpiler->name_pool = name_pool_create(transpiler->pool, nullptr);
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

SchemaValidator* SchemaValidator::create(Pool* pool) {
    if (!pool) return nullptr;

    SchemaValidator* validator = (SchemaValidator*)pool_calloc(pool, sizeof(SchemaValidator));
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

void SchemaValidator::destroy() {
    if (this->type_definitions) {
        hashmap_free(this->type_definitions);
    }

    if (this->visited_nodes) {
        hashmap_free(this->visited_nodes);
    }

    if (this->transpiler) {
        transpiler_destroy(this->transpiler);
    }

    // Note: Memory pool cleanup handled by caller
}

int SchemaValidator::load_schema(const char* source, const char* root_type) {
    if (!source || !root_type) return -1;

    log_info("Loading schema with root type: %s", root_type);

    // Build AST using transpiler
    AstNode* ast = transpiler_build_ast(this->transpiler, source);
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
                // After refactoring, AST_NODE_TYPE_STAM is an AstLetNode wrapper
                // The actual type assignment(s) are in the declare field, potentially chained via next
                AstLetNode* type_stam = (AstLetNode*)child;

                // Process all declarations in the type statement (could be chained)
                AstNode* declare_node = type_stam->declare;
                while (declare_node) {
                    if (declare_node->node_type != AST_NODE_ASSIGN) {
                        log_warn("Skipping non-ASSIGN declare node (type=%d)", declare_node->node_type);
                        declare_node = declare_node->next;
                        continue;
                    }

                    AstNamedNode* type_node = (AstNamedNode*)declare_node;

                    if (!type_node->name || !type_node->type) {
                        log_warn("Skipping type node without name or type");
                        declare_node = declare_node->next;
                        continue;
                    }

                    // Unwrap the TypeType* to get the actual Type*
                    // type_node->type is a TypeType* wrapper (e.g., &LIT_TYPE_STRING)
                    // We need to extract the underlying Type* from it
                    Type* actual_type = nullptr;
                    if (type_node->type->type_id == LMD_TYPE_TYPE) {
                        // It's a TypeType wrapper, unwrap it
                        TypeType* type_wrapper = (TypeType*)type_node->type;
                        actual_type = type_wrapper->type;
                    } else {
                        // For other cases, use the type directly
                        actual_type = type_node->type;
                    }

                    if (!actual_type) {
                        log_warn("Skipping type node with null actual type");
                        declare_node = declare_node->next;
                        continue;
                    }

                    // Create TypeDefinition
                    TypeDefinition* def = (TypeDefinition*)pool_calloc(this->pool, sizeof(TypeDefinition));
                    if (!def) {
                        log_error("Failed to allocate TypeDefinition");
                        return -1;
                    }

                    def->name.str = type_node->name->chars;
                    def->name.length = type_node->name->len;
                    def->runtime_type = actual_type;  // Use the unwrapped type
                    def->schema_type = nullptr;  // Can be filled in later if needed
                    def->is_exported = true;  // Assume exported by default

                    // Create TypeRegistryEntry for hashmap
                    TypeRegistryEntry entry;
                    entry.definition = def;
                    entry.name_key = def->name;

                    hashmap_set(this->type_definitions, &entry);

                    log_debug("Registered type: %.*s (type_id=%d)",
                        (int)def->name.length, def->name.str,
                        actual_type->type_id);
                    type_count++;

                    declare_node = declare_node->next;
                }
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

Type* SchemaValidator::find_type(const char* type_name) {
    if (!type_name) return nullptr;

    StrView name_view = {.str = type_name, .length = strlen(type_name)};
    TypeRegistryEntry key = {.definition = nullptr, .name_key = name_view};

    const TypeRegistryEntry* entry = (const TypeRegistryEntry*)hashmap_get(this->type_definitions, &key);
    return entry && entry->definition ? entry->definition->runtime_type : nullptr;
}

// Resolve a type reference with circular reference detection
// Returns the resolved Type* or nullptr if not found or circular
Type* SchemaValidator::resolve_type_reference(const char* type_name) {
    if (!type_name) return nullptr;

    StrView name_view = {.str = type_name, .length = strlen(type_name)};

    // Check if we're already visiting this type (circular reference)
    VisitedEntry visited_key = {.key = name_view, .visited = false};
    const VisitedEntry* existing = (const VisitedEntry*)hashmap_get(this->visited_nodes, &visited_key);

    if (existing && existing->visited) {
        log_error("[AST_VALIDATOR] Circular type reference detected: %.*s",
                  (int)name_view.length, name_view.str);
        return nullptr;
    }

    // Mark this type as being visited
    VisitedEntry visit_entry = {.key = name_view, .visited = true};
    hashmap_set(this->visited_nodes, &visit_entry);

    // Look up the type
    Type* resolved_type = this->find_type(type_name);

    // Unmark after resolution (allow revisiting in different validation paths)
    VisitedEntry unvisit_entry = {.key = name_view, .visited = false};
    hashmap_set(this->visited_nodes, &unvisit_entry);

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
                str_copy(error->message->chars, error->message->len + 1, message, error->message->len);
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

ValidationError* create_validation_error_ex(
    ValidationErrorCode code,
    const char* message,
    PathSegment* path,
    Type* expected_type,
    ConstItem actual_item,
    Pool* pool) {

    // Create basic error first
    ValidationError* error = create_validation_error(code, message, path, pool);
    if (!error) return nullptr;

    // Add expected type information
    error->expected = expected_type;

    // Copy actual item
    error->actual = *(Item*)&actual_item;

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

// validate 'item' against type named 'type_name'
ValidationResult* SchemaValidator::validate(ConstItem item, const char* type_name) {
    if (!type_name) {
        ValidationResult* result = create_validation_result(this->pool);
        if (result) {
            add_validation_error(result, create_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, "Invalid type name",
                nullptr, this->pool));
        }
        return result;
    }

    // Use resolve_type_reference for circular reference detection
    Type* type = this->resolve_type_reference(type_name);
    if (!type) {
        ValidationResult* result = create_validation_result(this->pool);
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "Type not found or circular reference detected: %s", type_name);
        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_REFERENCE_ERROR, error_msg, nullptr, this->pool);
        add_validation_error(result, error);
        return result;
    }

    return this->validate_type(item, type);
}

// validate 'item' against Type* directly
ValidationResult* SchemaValidator::validate_type(ConstItem item, Type* type) {
    if (!type) {
        ValidationResult* result = create_validation_result(this->pool);
        if (result) {
            add_validation_error(result, create_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, "Invalid type pointer",
                nullptr, this->pool));
        }
        return result;
    }

    // reset validation state
    this->current_path = nullptr;
    this->current_depth = 0;

    // initialize validation session for timeout tracking
    if (this->options.timeout_ms > 0) {
        this->validation_start_time = clock();
    }

    return validate_against_type(this, item, type);
}

// ==================== Validation Options Functions ====================
/**
 * Set validation options
 */
void SchemaValidator::set_options(ValidationOptions* options) {
    if (!options) return;
    this->options = *options;
}

/**
 * Get current validation options
 */
ValidationOptions* SchemaValidator::get_options() {
    return &this->options;
}

/**
 * Convenience: Set strict mode
 */
void SchemaValidator::set_strict_mode(bool strict) {
    this->options.strict_mode = strict;
}

/**
 * Convenience: Set maximum error count
 */
void SchemaValidator::set_max_errors(int max) {
    this->options.max_errors = max;
}

/**
 * Convenience: Set validation timeout
 */
void SchemaValidator::set_timeout(int timeout_ms) {
    this->options.timeout_ms = timeout_ms;
}

/**
 * Convenience: Enable error suggestions
 */
void SchemaValidator::set_show_suggestions(bool show) {
    this->options.show_suggestions = show;
}

/**
 * Convenience: Enable error context display
 */
void SchemaValidator::set_show_context(bool show) {
    this->options.show_context = show;
}

// ==================== Format-Specific Validation ====================

/**
 * Detect input format from item structure
 */
const char* detect_input_format(ConstItem item) {
    ItemReader reader(item);

    // check if it's an element (likely XML/HTML)
    if (reader.isElement()) {
        ElementReader element = reader.asElement();
        const char* tag = element.tagName();

        // check for common HTML root elements
        if (tag && (strcmp(tag, "html") == 0 ||
                   strcmp(tag, "body") == 0 ||
                   strcmp(tag, "head") == 0)) {
            return "html";
        }

        // check for XML document wrapper
        if (tag && strcmp(tag, "document") == 0) {
            return "xml";
        }

        return "xml"; // default for elements
    }

    // check if it's a map (likely JSON)
    if (reader.isMap()) {
        return "json";
    }

    // check if it's an array or list (could be JSON array)
    if (reader.isArray() || reader.isList()) {
        return "json";
    }

    return nullptr; // unknown format
}

/**
 * Unwrap XML document wrapper element
 * XML parsers often wrap content in a <document> root element that's not part of the schema
 */
ConstItem unwrap_xml_document(ConstItem item, Pool* pool) {
    ItemReader reader(item);

    // only unwrap if it's an element
    if (!reader.isElement()) {
        return item;
    }

    ElementReader element = reader.asElement();
    const char* tag = element.tagName();

    // check if this is a document wrapper
    if (!tag || strcmp(tag, "document") != 0) {
        return item; // not a document wrapper
    }

    log_debug("Detected XML <document> wrapper, unwrapping...");

    // find the first non-processing-instruction, non-comment child element
    int64_t child_count = element.childCount();
    for (int64_t i = 0; i < child_count; i++) {
        ItemReader child_reader = element.childAt(i);

        // skip non-element children (text nodes, comments, etc.)
        if (!child_reader.isElement()) {
            continue;
        }

        ElementReader child_element = child_reader.asElement();
        const char* child_tag = child_element.tagName();

        // skip processing instructions and comments
        if (child_tag && (strncmp(child_tag, "?", 1) == 0 ||
                         strcmp(child_tag, "!--") == 0)) {
            continue;
        }

        // found the actual content element
        log_debug("Found actual content element: <%s>", child_tag ? child_tag : "unknown");
        return child_reader.item().to_const();
    }

    // no suitable child found, return original item
    log_debug("No content element found in <document> wrapper");
    return item;
}

/**
 * Unwrap HTML document wrapper element
 * Handle HTML-specific quirks and wrapper elements
 */
ConstItem unwrap_html_document(ConstItem item, Pool* pool) {
    ItemReader reader(item);

    // only unwrap if it's an element
    if (!reader.isElement()) {
        return item;
    }

    ElementReader element = reader.asElement();
    const char* tag = element.tagName();

    // check if this is an html root wrapper
    if (tag && strcmp(tag, "html") == 0) {
        log_debug("Detected HTML <html> wrapper, looking for body...");

        // try to find <body> element
        int64_t child_count = element.childCount();
        for (int64_t i = 0; i < child_count; i++) {
            ItemReader child_reader = element.childAt(i);

            if (child_reader.isElement()) {
                ElementReader child_element = child_reader.asElement();
                const char* child_tag = child_element.tagName();

                if (child_tag && strcmp(child_tag, "body") == 0) {
                    log_debug("Found <body> element, unwrapping...");
                    return child_reader.item().to_const();
                }
            }
        }
    }

    // not an HTML wrapper or couldn't find body
    return item;
}

// ==================== C Wrapper Functions ====================

extern "C" {

SchemaValidator* schema_validator_create(Pool* pool) {
    return SchemaValidator::create(pool);
}

void schema_validator_destroy(SchemaValidator* validator) {
    if (validator) {
        validator->destroy();
    }
}

int schema_validator_load_schema(SchemaValidator* validator, const char* source, const char* root_type) {
    if (!validator) return -1;
    return validator->load_schema(source, root_type);
}

Type* schema_validator_find_type(SchemaValidator* validator, const char* type_name) {
    if (!validator) return nullptr;
    return validator->find_type(type_name);
}

Type* schema_validator_resolve_type_reference(SchemaValidator* validator, const char* type_name) {
    if (!validator) return nullptr;
    return validator->resolve_type_reference(type_name);
}

ValidationResult* schema_validator_validate(SchemaValidator* validator, ConstItem item, const char* type_name) {
    if (!validator) return nullptr;
    return validator->validate(item, type_name);
}

ValidationResult* schema_validator_validate_type(SchemaValidator* validator, ConstItem item, Type* type) {
    if (!validator) {
        // return an invalid result for null validator
        ValidationResult* result = (ValidationResult*)calloc(1, sizeof(ValidationResult));
        result->valid = false;
        result->error_count = 0;
        result->errors = nullptr;
        return result;
    }
    return validator->validate_type(item, type);
}

ValidationResult* schema_validator_validate_with_format(
    SchemaValidator* validator,
    ConstItem item,
    const char* type_name,
    const char* input_format
) {
    if (!validator) return nullptr;
    return validator->validate_with_format(item, type_name, input_format);
}

ValidationOptions schema_validator_default_options() {
    ValidationOptions opts = {};
    opts.strict_mode = false;
    opts.allow_unknown_fields = false;
    opts.allow_empty_elements = false;
    opts.max_depth = 100;
    opts.timeout_ms = 0;
    opts.max_errors = 0;
    opts.show_suggestions = true;
    opts.show_context = true;
    opts.enabled_rules = nullptr;
    opts.disabled_rules = nullptr;
    return opts;
}

void schema_validator_set_options(SchemaValidator* validator, ValidationOptions* options) {
    if (!validator) return;
    validator->set_options(options);
}

ValidationOptions* schema_validator_get_options(SchemaValidator* validator) {
    if (!validator) return nullptr;
    return validator->get_options();
}

void schema_validator_set_strict_mode(SchemaValidator* validator, bool strict) {
    if (!validator) return;
    validator->set_strict_mode(strict);
}

void schema_validator_set_max_errors(SchemaValidator* validator, int max) {
    if (!validator) return;
    validator->set_max_errors(max);
}

void schema_validator_set_timeout(SchemaValidator* validator, int timeout_ms) {
    if (!validator) return;
    validator->set_timeout(timeout_ms);
}

void schema_validator_set_show_suggestions(SchemaValidator* validator, bool show) {
    if (!validator) return;
    validator->set_show_suggestions(show);
}

void schema_validator_set_show_context(SchemaValidator* validator, bool show) {
    if (!validator) return;
    validator->set_show_context(show);
}

} // extern "C"

/**
 * Validate with format-specific handling
 */
ValidationResult* SchemaValidator::validate_with_format(
    ConstItem item,
    const char* type_name,
    const char* input_format
) {
    if (!type_name) {
        return nullptr;
    }

    // auto-detect format if not specified
    if (!input_format) {
        input_format = detect_input_format(item);
    }

    log_debug("Validating with format: %s", input_format ? input_format : "auto");

    // apply format-specific unwrapping
    ConstItem unwrapped_item = [&]() {
        if (input_format) {
            if (strcmp(input_format, "xml") == 0) {
                return unwrap_xml_document(item, this->pool);
            } else if (strcmp(input_format, "html") == 0) {
                return unwrap_html_document(item, this->pool);
            }
        }
        // json and other formats don't need unwrapping
        return item;
    }();

    // perform standard validation on unwrapped item
    return this->validate(unwrapped_item, type_name);
}
