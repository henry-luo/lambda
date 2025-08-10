/**
 * @file validator.cpp
 * @brief Lambda Schema Validator - Core Implementation (C++)
 * @author Henry Luo
 * @license MIT
 */

#include "validator.h"
#include <cstring>
#include <cassert>
#include <memory>
#include <string>

// ==================== Hashmap Entry Structures ====================

struct SchemaEntry {
    StrView name;
    TypeSchema* schema;
};

struct VisitedEntry {
    StrView key;
    bool visited;
};

// ==================== Hashmap Helper Functions ====================

// Hash function for StrView keys
uint64_t strview_hash(const void *item, uint64_t seed0, uint64_t seed1) {
    const StrView* key = (const StrView*)item;
    return hashmap_sip(key->str, key->length, seed0, seed1);
}

// Compare function for StrView keys  
int strview_compare(const void *a, const void *b, void *udata) {
    const StrView* key_a = (const StrView*)a;
    const StrView* key_b = (const StrView*)b;
    
    if (key_a->length != key_b->length) {
        return (key_a->length < key_b->length) ? -1 : 1;
    }
    return memcmp(key_a->str, key_b->str, key_a->length);
}

// ==================== Schema Validator Creation ====================

SchemaValidator* schema_validator_create(VariableMemPool* pool) {
    SchemaValidator* validator = (SchemaValidator*)pool_calloc(pool, sizeof(SchemaValidator));
    if (!validator) return nullptr;
    
    validator->pool = pool;
    
    // Create C hashmap for schemas 
    validator->schemas = hashmap_new(
        sizeof(SchemaEntry), 16, 0, 1,
        strview_hash, strview_compare, nullptr, pool
    );
    
    if (!validator->schemas) {
        return nullptr;
    }
    
    validator->context = (ValidationContext*)pool_calloc(pool, sizeof(ValidationContext));
    if (!validator->context) {
        return nullptr;
    }
    
    validator->custom_validators = nullptr;
    
    // Initialize default options
    validator->default_options.strict_mode = false;
    validator->default_options.allow_unknown_fields = true;
    validator->default_options.allow_empty_elements = false;
    validator->default_options.max_depth = 100;
    validator->default_options.timeout_ms = 0;
    
    // Initialize validation context
    validator->context->pool = pool;
    validator->context->path = nullptr;
    validator->context->schema_registry = validator->schemas;
    
    // Create visited tracking hashmap
    validator->context->visited = hashmap_new(
        sizeof(VisitedEntry), 16, 0, 1,
        strview_hash, strview_compare, nullptr, pool
    );
    
    if (!validator->context->visited) {
        return nullptr;
    }
    
    validator->context->custom_validators = nullptr;
    validator->context->options = validator->default_options;
    validator->context->current_depth = 0;
    
    return validator;
}

void schema_validator_destroy(SchemaValidator* validator) {
    if (!validator) return;
    
    // Cleanup hashmaps
    if (validator->schemas) {
        hashmap_free(validator->schemas);
    }
    if (validator->context && validator->context->visited) {
        hashmap_free(validator->context->visited);
    }
    // Note: memory pool cleanup handled by caller
}

// ==================== Schema Loading ====================

int schema_validator_load_schema(SchemaValidator* validator, const char* schema_source, 
                                 const char* schema_name) {
    if (!validator || !schema_source || !schema_name) return -1;
    
    SchemaParser* parser = schema_parser_create(validator->pool);
    if (!parser) return -1;
    
    TypeSchema* schema = parse_schema_from_source(parser, schema_source);
    if (!schema) {
        schema_parser_destroy(parser);
        return -1;
    }
    
    // Store schema in C hashmap registry
    StrView name_view = strview_from_cstr(schema_name);
    SchemaEntry entry = { .name = name_view, .schema = schema };
    const void* result = hashmap_set(validator->schemas, &entry);
    if (result == NULL && hashmap_oom(validator->schemas)) {
        schema_parser_destroy(parser);
        return -1;
    }
    
    schema_parser_destroy(parser);
    return 0;
}

// ==================== Validation Engine ====================

ValidationResult* validate_item(SchemaValidator* validator, Item item, 
                                TypeSchema* schema, ValidationContext* context) {
    if (!validator || !schema || !context) {
        ValidationResult* result = create_validation_result(context ? context->pool : validator->pool);
        add_validation_error(result, create_validation_error(
            VALID_ERROR_PARSE_ERROR, "Invalid validation parameters", 
            context ? context->path : nullptr, 
            context ? context->pool : validator->pool));
        return result;
    }
    
    // Check validation depth
    if (context->current_depth >= context->options.max_depth) {
        ValidationResult* result = create_validation_result(context->pool);
        add_validation_error(result, create_validation_error(
            VALID_ERROR_CONSTRAINT_VIOLATION, "Maximum validation depth exceeded", 
            context->path, context->pool));
        return result;
    }
    
    context->current_depth++;
    
    ValidationResult* result = nullptr;
    
    // Dispatch to appropriate validation function based on schema type
    switch (schema->schema_type) {
        case LMD_SCHEMA_PRIMITIVE:
            result = validate_primitive(item, schema, context);
            break;
        case LMD_SCHEMA_UNION:
            result = validate_union(validator, item, schema, context);
            break;
        case LMD_SCHEMA_ARRAY:
            result = validate_array(validator, item, schema, context);
            break;
        case LMD_SCHEMA_MAP:
            result = validate_map(validator, item, schema, context);
            break;
        case LMD_SCHEMA_ELEMENT:
            result = validate_element(validator, item, schema, context);
            break;
        case LMD_SCHEMA_OCCURRENCE:
            result = validate_occurrence(validator, item, schema, context);
            break;
        case LMD_SCHEMA_REFERENCE:
            result = validate_reference(validator, item, schema, context);
            break;
        case LMD_SCHEMA_LITERAL:
            result = validate_literal(item, schema, context);
            break;
        default:
            result = create_validation_result(context->pool);
            add_validation_error(result, create_validation_error(
                VALID_ERROR_TYPE_MISMATCH, "Unknown schema type", 
                context->path, context->pool));
            break;
    }
    
    // Run custom validators if any
    CustomValidator* custom = context->custom_validators;
    while (custom && result->valid) {
        ValidationResult* custom_result = custom->func(item, schema, context);
        if (custom_result) {
            merge_validation_results(result, custom_result);
        }
        custom = custom->next;
    }
    
    context->current_depth--;
    return result;
}

// ==================== Primitive Type Validation ====================

ValidationResult* validate_primitive(Item item, TypeSchema* schema, ValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);
    
    if (schema->schema_type != LMD_SCHEMA_PRIMITIVE) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected primitive schema", 
            ctx->path, ctx->pool));
        return result;
    }
    
    SchemaPrimitive* prim_schema = (SchemaPrimitive*)schema->schema_data;
    TypeId expected_type = prim_schema->primitive_type;
    TypeId actual_type = get_type_id(item);
    
    if (!is_compatible_type(actual_type, expected_type)) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: expected type %d, got type %d",
                expected_type, actual_type);
        
        result->valid = false;
        result->error_count = 1;
        
        /*
        ValidationError* error = create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->path, ctx->pool);
        if (error) {
            error->expected = schema;
            error->actual = item;
            add_validation_error(result, error);
        }
        */
    }
    
    return result;
}

// ==================== Array Validation ====================

ValidationResult* validate_array(SchemaValidator* validator, Item item, TypeSchema* schema, ValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);
    
    if (schema->schema_type != LMD_SCHEMA_ARRAY) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected array schema", 
            ctx->path, ctx->pool));
        return result;
    }
    
    TypeId actual_type = get_type_id(item);
    if (actual_type != LMD_TYPE_ARRAY && actual_type != LMD_TYPE_LIST) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected array or list", 
            ctx->path, ctx->pool));
        return result;
    }
    
    SchemaArray* array_schema = (SchemaArray*)schema->schema_data;
    List* list = (List*)item.pointer;
    
    // Check occurrence constraints
    if (array_schema->occurrence == '+' && list->length == 0) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_OCCURRENCE_ERROR, "Array must have at least one element (+)", 
            ctx->path, ctx->pool));
        return result;
    }
    
    // Validate each element
    if (array_schema->element_type) {
        for (long i = 0; i < list->length; i++) {
            Item element = list_get(list, i);
            
            // Create path for this element
            PathSegment* element_path = path_push_index(ctx->path, i, ctx->pool);
            ValidationContext element_ctx = *ctx;
            element_ctx.path = element_path;
            
            ValidationResult* element_result = validate_item(
                validator, element, array_schema->element_type, &element_ctx);
            
            if (element_result) {
                merge_validation_results(result, element_result);
            }
        }
    }
    
    return result;
}

// ==================== Map Validation ====================

ValidationResult* validate_map(SchemaValidator* validator, Item item, TypeSchema* schema, ValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);
    
    if (schema->schema_type != LMD_SCHEMA_MAP) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected map schema", 
            ctx->path, ctx->pool));
        return result;
    }
    
    TypeId actual_type = get_type_id(item);
    
    if (actual_type != LMD_TYPE_MAP && actual_type != LMD_TYPE_ELEMENT) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected map", 
            ctx->path, ctx->pool));
        return result;
    }
    
    SchemaMap* map_schema = (SchemaMap*)schema->schema_data;
    Map* map = (Map*)item.pointer;
    
    // NOTE: The schema parser currently has incomplete field parsing implementation
    // This is why schema validation is too permissive - the map schema doesn't 
    // contain the actual field definitions from the Document type
    
    // Validate required fields and check types
    SchemaMapField* field = map_schema->fields;
    while (field) {
        Item field_key = {.item = s2it(string_from_strview(field->name, ctx->pool))};
        Item field_value = map_get(map, field_key);
        
        if (field_value.item == ITEM_NULL) {
            if (field->required) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "Missing required field: %.*s", 
                        (int)field->name.length, field->name.str);
                
                PathSegment* field_path = path_push_field(ctx->path, 
                    field->name.str, ctx->pool);
                add_validation_error(result, create_validation_error(
                    VALID_ERROR_MISSING_FIELD, error_msg, field_path, ctx->pool));
            }
        } else {
            // Validate field value
            PathSegment* field_path = path_push_field(ctx->path, 
                field->name.str, ctx->pool);
            ValidationContext field_ctx = *ctx;
            field_ctx.path = field_path;
            
            ValidationResult* field_result = validate_item(
                validator, field_value, field->type, &field_ctx);
            
            if (field_result) {
                merge_validation_results(result, field_result);
            }
        }
        
        field = field->next;
    }
    
    // TODO: Check for unexpected fields if not open
    
    return result;
}

// ==================== Element Validation ====================

ValidationResult* validate_element(SchemaValidator* validator, Item item, TypeSchema* schema, ValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);
    
    if (schema->schema_type != LMD_SCHEMA_ELEMENT) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected element schema", 
            ctx->path, ctx->pool));
        return result;
    }
    
    TypeId actual_type = get_type_id(item);
    if (actual_type != LMD_TYPE_ELEMENT) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected element", 
            ctx->path, ctx->pool));
        return result;
    }
    
    SchemaElement* element_schema = (SchemaElement*)schema->schema_data;
    Element* element = (Element*)item.pointer;
    
    // Check element tag matches
    if (element->type) {
        TypeElmt* elmt_type = (TypeElmt*)element->type;
        if (element_schema->tag.length > 0) {
            // Compare element tag name with schema tag
            if (elmt_type->name.length != element_schema->tag.length ||
                memcmp(elmt_type->name.str, element_schema->tag.str, element_schema->tag.length) != 0) {
                
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), 
                        "Element tag mismatch: expected <%.*s>, got <%.*s>",
                        (int)element_schema->tag.length, element_schema->tag.str,
                        (int)elmt_type->name.length, elmt_type->name.str);
                
                add_validation_error(result, create_validation_error(
                    VALID_ERROR_INVALID_ELEMENT, error_msg, 
                    ctx->path, ctx->pool));
                return result;
            }
        }
    }
    
    // Validate attributes
    if (element_schema->attributes) {
        SchemaMapField* required_attr = element_schema->attributes;
        while (required_attr) {
            // Check if required attribute exists
            Item attr_key = {.item = s2it(string_from_strview(required_attr->name, ctx->pool))};
            Item attr_value = elmt_get(element, attr_key);
            
            if (attr_value.item == ITEM_NULL) {
                if (required_attr->required) {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), 
                            "Missing required attribute: %.*s", 
                            (int)required_attr->name.length, required_attr->name.str);
                    
                    PathSegment* attr_path = path_push_attribute(ctx->path, 
                        required_attr->name.str, ctx->pool);
                    add_validation_error(result, create_validation_error(
                        VALID_ERROR_MISSING_FIELD, error_msg, attr_path, ctx->pool));
                }
            } else {
                // Validate attribute type
                PathSegment* attr_path = path_push_attribute(ctx->path, 
                    required_attr->name.str, ctx->pool);
                ValidationContext attr_ctx = *ctx;
                attr_ctx.path = attr_path;
                
                ValidationResult* attr_result = validate_item(
                    validator, attr_value, required_attr->type, &attr_ctx);
                
                if (attr_result) {
                    merge_validation_results(result, attr_result);
                }
            }
            
            required_attr = required_attr->next;
        }
    }
    
    // Validate content types
    if (element_schema->content_count > 0 && element_schema->content_types) {
        for (int i = 0; i < element_schema->content_count; i++) {
            if (i < element->length) {
                Item content_item = element->items[i];
                
                PathSegment* content_path = path_push_index(ctx->path, i, ctx->pool);
                ValidationContext content_ctx = *ctx;
                content_ctx.path = content_path;
                
                ValidationResult* content_result = validate_item(
                    validator, content_item, element_schema->content_types[i], &content_ctx);
                
                if (content_result) {
                    merge_validation_results(result, content_result);
                }
            }
        }
        
        // Check if element has too many content items
        if (element->length > element_schema->content_count) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Element has %ld content items, but schema allows only %d",
                    element->length, element_schema->content_count);
            
            add_validation_error(result, create_validation_error(
                VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, 
                ctx->path, ctx->pool));
        }
    }
    
    return result;
}

// ==================== Union Validation ====================

ValidationResult* validate_union(SchemaValidator* validator, Item item, TypeSchema* schema, ValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);
    
    if (schema->schema_type != LMD_SCHEMA_UNION) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected union schema", 
            ctx->path, ctx->pool));
        return result;
    }
    
    SchemaUnion* union_schema = (SchemaUnion*)schema->schema_data;
    
    // Try to validate against each type in the union
    for (int i = 0; i < union_schema->type_count; i++) {
        ValidationResult* type_result = validate_item(
            validator, item, union_schema->types[i], ctx);
        
        if (type_result && type_result->valid) {
            // Found matching type in union
            validation_result_destroy(result);
            return type_result;
        }
        
        if (type_result) {
            validation_result_destroy(type_result);
        }
    }
    
    // No types in union matched
    add_validation_error(result, create_validation_error(
        VALID_ERROR_TYPE_MISMATCH, 
        "Value does not match any type in union", 
        ctx->path, ctx->pool));
    
    return result;
}

// ==================== Occurrence Validation ====================

ValidationResult* validate_occurrence(SchemaValidator* validator, Item item, TypeSchema* schema, ValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);
    
    if (schema->schema_type != LMD_SCHEMA_OCCURRENCE) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected occurrence schema", 
            ctx->path, ctx->pool));
        return result;
    }
    
    SchemaOccurrence* occur_schema = (SchemaOccurrence*)schema->schema_data;
    
    switch (occur_schema->modifier) {
        case '?': // Optional
            if (item.item == ITEM_NULL) {
                // Null is valid for optional types
                return result;
            }
            // Fall through to validate the base type
            validation_result_destroy(result);
            return validate_item(validator, item, occur_schema->base_type, ctx);
            
        case '+': // One or more
        case '*': // Zero or more
            // These should be handled by array validation
            validation_result_destroy(result);
            return validate_array(validator, item, occur_schema->base_type, ctx);
            
        default:
            add_validation_error(result, create_validation_error(
                VALID_ERROR_OCCURRENCE_ERROR, "Invalid occurrence modifier", 
                ctx->path, ctx->pool));
            return result;
    }
}

// ==================== Reference Validation ====================

ValidationResult* validate_reference(SchemaValidator* validator, Item item, TypeSchema* schema, ValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);
    
    if (schema->schema_type != LMD_SCHEMA_REFERENCE) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected reference schema", 
            ctx->path, ctx->pool));
        return result;
    }
    
    // Resolve the reference
    TypeSchema* resolved = resolve_reference(schema, ctx->schema_registry);
    if (!resolved) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Cannot resolve type reference: %.*s",
                (int)schema->name.length, schema->name.str);
        
        add_validation_error(result, create_validation_error(
            VALID_ERROR_REFERENCE_ERROR, error_msg, ctx->path, ctx->pool));
        return result;
    }
    
    // Check for circular references
    VisitedEntry lookup = { .key = schema->name, .visited = false };
    const VisitedEntry* visited_entry = (const VisitedEntry*)hashmap_get(ctx->visited, &lookup);
    if (visited_entry && visited_entry->visited) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_CIRCULAR_REFERENCE, "Circular type reference detected", 
            ctx->path, ctx->pool));
        return result;
    }
    
    // Mark as visited and validate
    VisitedEntry entry = { .key = schema->name, .visited = true };
    hashmap_set(ctx->visited, &entry);
    
    validation_result_destroy(result);
    result = validate_item(validator, item, resolved, ctx);
    
    // Unmark as visited
    VisitedEntry unmark_entry = { .key = schema->name, .visited = false };
    hashmap_set(ctx->visited, &unmark_entry);
    
    return result;
}

// ==================== Literal Validation ====================

ValidationResult* validate_literal(Item item, TypeSchema* schema, ValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);
    
    if (schema->schema_type != LMD_SCHEMA_LITERAL) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected literal schema", 
            ctx->path, ctx->pool));
        return result;
    }
    
    SchemaLiteral* literal_schema = (SchemaLiteral*)schema->schema_data;
    
    // Compare items directly
    if (item.item != literal_schema->literal_value.item) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Value does not match literal", 
            ctx->path, ctx->pool));
    }
    
    return result;
}

// ==================== Validation Result Management ====================

ValidationResult* create_validation_result(VariableMemPool* pool) {
    ValidationResult* result = (ValidationResult*)pool_calloc(pool, sizeof(ValidationResult));
    result->valid = true;
    result->errors = NULL;
    result->warnings = NULL;
    result->error_count = 0;
    result->warning_count = 0;
    return result;
}

void add_validation_error(ValidationResult* result, ValidationError* error) {
    if (!result || !error) return;
    
    error->next = result->errors;
    result->errors = error;
    result->error_count++;
    result->valid = false;
}

void merge_validation_results(ValidationResult* dest, ValidationResult* src) {
    if (!dest || !src) return;
    
    // Merge errors
    ValidationError* error = src->errors;
    while (error) {
        ValidationError* next = error->next;
        error->next = dest->errors;
        dest->errors = error;
        dest->error_count++;
        error = next;
    }
    
    // Merge warnings
    ValidationWarning* warning = src->warnings;
    while (warning) {
        ValidationWarning* next = warning->next;
        warning->next = dest->warnings;
        dest->warnings = warning;
        dest->warning_count++;
        warning = next;
    }
    
    if (src->error_count > 0) {
        dest->valid = false;
    }
    
    // Clear source lists to avoid double-free
    src->errors = NULL;
    src->warnings = NULL;
}

// ==================== Error Creation ====================

ValidationError* create_validation_error(ValidationErrorCode code, const char* message,
                                        PathSegment* path, VariableMemPool* pool) {
    ValidationError* error = (ValidationError*)pool_calloc(pool, sizeof(ValidationError));
    error->code = code;
    error->message = string_from_strview(strview_from_cstr(message), pool);
    error->path = path;
    error->expected = NULL;
    error->actual = (Item){.item = ITEM_NULL};
    error->suggestions = NULL;
    error->next = NULL;
    return error;
}

// ==================== Utility Functions ====================

bool is_compatible_type(TypeId actual, TypeId expected) {
    if (actual == expected) return true;
    
    // Special compatibility rules
    switch (expected) {
        case LMD_TYPE_NUMBER:
            return actual == LMD_TYPE_INT || actual == LMD_TYPE_INT64 || 
                   actual == LMD_TYPE_FLOAT || actual == LMD_TYPE_DECIMAL;
        case LMD_TYPE_ANY:
            return true;
        default:
            return false;
    }
}

StrView strview_from_cstr(const char* str) {
    StrView view = {.str = str, .length = str ? strlen(str) : 0};
    return view;
}

String* string_from_strview(StrView view, VariableMemPool* pool) {
    if (view.length == 0) return &EMPTY_STRING;
    
    if (!pool) {
        printf("Error: string_from_strview called with NULL pool!\n");
        return &EMPTY_STRING;
    }
    
    String* str;
    MemPoolError err = pool_variable_alloc(pool, sizeof(String) + view.length + 1, (void**)&str);
    if (err != MEM_POOL_ERR_OK) {
        printf("Error: pool_variable_alloc failed with error %d\n", err);
        return &EMPTY_STRING;
    }
    
    str->len = view.length;
    str->ref_cnt = 1;
    memcpy(str->chars, view.str, view.length);
    str->chars[view.length] = '\0';
    return str;
}

TypeSchema* resolve_reference(TypeSchema* ref_schema, HashMap* registry) {
    if (!ref_schema || ref_schema->schema_type != LMD_SCHEMA_REFERENCE) {
        return nullptr;
    }
    
    SchemaEntry lookup = { .name = ref_schema->name };
    const SchemaEntry* entry = (const SchemaEntry*)hashmap_get(registry, &lookup);
    return entry ? entry->schema : nullptr;
}

// ==================== Missing Implementation Functions ====================

// Schema creation helper functions
TypeSchema* create_primitive_schema(TypeId primitive_type, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->schema_type = LMD_SCHEMA_PRIMITIVE;
    
    SchemaPrimitive* prim_data = (SchemaPrimitive*)pool_calloc(pool, sizeof(SchemaPrimitive));
    prim_data->primitive_type = primitive_type;
    schema->schema_data = prim_data;
    
    return schema;
}

TypeSchema* create_array_schema(TypeSchema* element_type, long min_len, long max_len, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->schema_type = LMD_SCHEMA_ARRAY;
    
    SchemaArray* array_data = (SchemaArray*)pool_calloc(pool, sizeof(SchemaArray));
    array_data->element_type = element_type;
    
    // Set occurrence based on min_len
    if (min_len == 0) {
        array_data->occurrence = (max_len == -1) ? '*' : '?';
    } else {
        array_data->occurrence = '+';
    }
    
    schema->schema_data = array_data;
    return schema;
}

TypeSchema* create_union_schema(List* types, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->schema_type = LMD_SCHEMA_UNION;
    
    SchemaUnion* union_data = (SchemaUnion*)pool_calloc(pool, sizeof(SchemaUnion));
    union_data->type_count = (int)types->length;
    union_data->types = (TypeSchema**)pool_calloc(pool, 
        sizeof(TypeSchema*) * union_data->type_count);
    
    for (int i = 0; i < union_data->type_count; i++) {
        Item type_item = list_get(types, i);
        union_data->types[i] = (TypeSchema*)type_item.pointer;
    }
    
    schema->schema_data = union_data;
    return schema;
}

TypeSchema* create_map_schema(TypeSchema* key_type, TypeSchema* value_type, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->schema_type = LMD_SCHEMA_MAP;
    
    SchemaMap* map_data = (SchemaMap*)pool_calloc(pool, sizeof(SchemaMap));
    map_data->fields = NULL;
    map_data->field_count = 0;
    map_data->is_open = true;
    
    schema->schema_data = map_data;
    return schema;
}

TypeSchema* create_element_schema(const char* tag_name, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->schema_type = LMD_SCHEMA_ELEMENT;
    
    SchemaElement* element_data = (SchemaElement*)pool_calloc(pool, sizeof(SchemaElement));
    element_data->tag = strview_from_cstr(tag_name);
    element_data->attributes = NULL;
    element_data->content_types = NULL;
    element_data->content_count = 0;
    element_data->is_open = true;
    
    schema->schema_data = element_data;
    return schema;
}

TypeSchema* create_occurrence_schema(TypeSchema* base_type, long min_count, long max_count, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->schema_type = LMD_SCHEMA_OCCURRENCE;
    
    SchemaOccurrence* occ_data = (SchemaOccurrence*)pool_calloc(pool, sizeof(SchemaOccurrence));
    occ_data->base_type = base_type;
    
    if (min_count == 0 && max_count == 1) {
        occ_data->modifier = '?';
    } else if (min_count == 1 && max_count == -1) {
        occ_data->modifier = '+';
    } else if (min_count == 0 && max_count == -1) {
        occ_data->modifier = '*';
    } else {
        occ_data->modifier = '?';  // default
    }
    
    schema->schema_data = occ_data;
    return schema;
}

TypeSchema* create_reference_schema(const char* type_name, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->schema_type = LMD_SCHEMA_REFERENCE;
    schema->name = strview_from_cstr(type_name);
    
    // No schema_data needed for references
    schema->schema_data = NULL;
    
    return schema;
}

TypeSchema* create_literal_schema(Item literal_value, VariableMemPool* pool) {
    TypeSchema* schema = (TypeSchema*)pool_calloc(pool, sizeof(TypeSchema));
    schema->schema_type = LMD_SCHEMA_LITERAL;
    
    SchemaLiteral* literal_data = (SchemaLiteral*)pool_calloc(pool, sizeof(SchemaLiteral));
    literal_data->literal_value = literal_value;
    
    schema->schema_data = literal_data;
    return schema;
}

// Path management functions
PathSegment* create_path_segment(PathSegmentType type, VariableMemPool* pool) {
    PathSegment* segment = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
    segment->type = type;
    segment->next = NULL;
    return segment;
}

PathSegment* create_field_path(const char* field_name, VariableMemPool* pool) {
    PathSegment* segment = create_path_segment(PATH_FIELD, pool);
    segment->data.field_name = strview_from_cstr(field_name);
    return segment;
}

PathSegment* create_index_path(long index, VariableMemPool* pool) {
    PathSegment* segment = create_path_segment(PATH_INDEX, pool);
    segment->data.index = index;
    return segment;
}

PathSegment* create_element_path(const char* tag_name, VariableMemPool* pool) {
    PathSegment* segment = create_path_segment(PATH_ELEMENT, pool);
    segment->data.element_tag = strview_from_cstr(tag_name);
    return segment;
}

PathSegment* push_path_segment(ValidationContext* ctx, PathSegment* segment) {
    PathSegment* old_path = ctx->path;
    segment->next = old_path;
    ctx->path = segment;
    return old_path;
}

void pop_path_segment(ValidationContext* ctx) {
    if (ctx->path) {
        ctx->path = ctx->path->next;
    }
}

PathSegment* path_push_field(PathSegment* path, const char* field_name, VariableMemPool* pool) {
    PathSegment* segment = create_field_path(field_name, pool);
    segment->next = path;
    return segment;
}

PathSegment* path_push_index(PathSegment* path, long index, VariableMemPool* pool) {
    PathSegment* segment = create_index_path(index, pool);
    segment->next = path;
    return segment;
}

PathSegment* path_push_element(PathSegment* path, const char* tag, VariableMemPool* pool) {
    PathSegment* segment = create_element_path(tag, pool);
    segment->next = path;
    return segment;
}

PathSegment* path_push_attribute(PathSegment* path, const char* attr_name, VariableMemPool* pool) {
    PathSegment* segment = create_path_segment(PATH_ATTRIBUTE, pool);
    segment->data.attr_name = strview_from_cstr(attr_name);
    segment->next = path;
    return segment;
}

// String formatting functions
String* format_validation_path(PathSegment* path, VariableMemPool* pool) {
    if (!path) {
        return string_from_strview(strview_from_cstr(""), pool);
    }
    
    // Calculate total length needed
    size_t total_len = 0;
    PathSegment* current = path;
    while (current) {
        switch (current->type) {
            case PATH_FIELD:
                total_len += 1 + current->data.field_name.length; // "." + field name
                break;
            case PATH_INDEX:
                total_len += 20; // "[" + number + "]" (assume max 20 chars for number)
                break;
            case PATH_ELEMENT:
                total_len += 2 + current->data.element_tag.length; // "<" + tag + ">"
                break;
            case PATH_ATTRIBUTE:
                total_len += 1 + current->data.attr_name.length; // "@" + attr name
                break;
        }
        current = current->next;
    }
    
    // Build path string (in reverse order)
    char* buffer = (char*)pool_calloc(pool, total_len + 1);
    char* pos = buffer;
    
    // Build path segments (reverse order since path is stored backwards)
    char temp_buffer[256];
    PathSegment* segments[100]; // assume max 100 path segments
    int segment_count = 0;
    
    current = path;
    while (current && segment_count < 100) {
        segments[segment_count++] = current;
        current = current->next;
    }
    
    // Now build string in correct order
    for (int i = segment_count - 1; i >= 0; i--) {
        PathSegment* segment = segments[i];
        switch (segment->type) {
            case PATH_FIELD:
                *pos++ = '.';
                memcpy(pos, segment->data.field_name.str, segment->data.field_name.length);
                pos += segment->data.field_name.length;
                break;
            case PATH_INDEX:
                snprintf(temp_buffer, sizeof(temp_buffer), "[%ld]", segment->data.index);
                strcpy(pos, temp_buffer);
                pos += strlen(temp_buffer);
                break;
            case PATH_ELEMENT:
                *pos++ = '<';
                memcpy(pos, segment->data.element_tag.str, segment->data.element_tag.length);
                pos += segment->data.element_tag.length;
                *pos++ = '>';
                break;
            case PATH_ATTRIBUTE:
                *pos++ = '@';
                memcpy(pos, segment->data.attr_name.str, segment->data.attr_name.length);
                pos += segment->data.attr_name.length;
                break;
        }
    }
    
    *pos = '\0';
    return string_from_strview(strview_from_cstr(buffer), pool);
}

String* format_type_name(TypeSchema* type, VariableMemPool* pool) {
    if (!type) {
        return string_from_strview(strview_from_cstr("unknown"), pool);
    }
    
    switch (type->schema_type) {
        case LMD_SCHEMA_PRIMITIVE: {
            SchemaPrimitive* prim = (SchemaPrimitive*)type->schema_data;
            if (prim->primitive_type <= LMD_TYPE_ERROR && prim->primitive_type >= 0) {
                return string_from_strview(strview_from_cstr(type_info[prim->primitive_type].name), pool);
            }
            return string_from_strview(strview_from_cstr("primitive"), pool);
        }
        case LMD_SCHEMA_ARRAY:
            return string_from_strview(strview_from_cstr("array"), pool);
        case LMD_SCHEMA_MAP:
            return string_from_strview(strview_from_cstr("map"), pool);
        case LMD_SCHEMA_ELEMENT:
            return string_from_strview(strview_from_cstr("element"), pool);
        case LMD_SCHEMA_UNION:
            return string_from_strview(strview_from_cstr("union"), pool);
        case LMD_SCHEMA_OCCURRENCE:
            return string_from_strview(strview_from_cstr("occurrence"), pool);
        case LMD_SCHEMA_REFERENCE:
            return string_from_strview(strview_from_cstr("reference"), pool);
        case LMD_SCHEMA_LITERAL:
            return string_from_strview(strview_from_cstr("literal"), pool);
        default:
            return string_from_strview(strview_from_cstr("unknown"), pool);
    }
}

String* format_validation_error(ValidationError* error, VariableMemPool* pool) {
    if (!error) {
        return string_from_strview(strview_from_cstr(""), pool);
    }
    
    char buffer[1024];
    String* path_str = format_validation_path(error->path, pool);
    
    snprintf(buffer, sizeof(buffer), "%s%s%s", 
             path_str->chars, 
             (path_str->len > 0) ? ": " : "",
             error->message ? error->message->chars : "Unknown error");
    
    return string_from_strview(strview_from_cstr(buffer), pool);
}

// ==================== Public API Implementation ====================

// Wrapper structure for public API
struct LambdaValidator {
    SchemaValidator* internal_validator;
    VariableMemPool* pool;
};

LambdaValidator* lambda_validator_create(void) {
    VariableMemPool* pool = NULL;
    if (pool_variable_init(&pool, 8192, 50) != MEM_POOL_ERR_OK) {
        return NULL;
    }
    
    LambdaValidator* validator = (LambdaValidator*)pool_calloc(pool, sizeof(LambdaValidator));
    validator->pool = pool;
    validator->internal_validator = schema_validator_create(pool);
    
    if (!validator->internal_validator) {
        pool_variable_destroy(pool);
        return NULL;
    }
    
    return validator;
}

void lambda_validator_destroy(LambdaValidator* validator) {
    if (!validator) return;
    
    if (validator->internal_validator) {
        schema_validator_destroy(validator->internal_validator);
    }
    
    if (validator->pool) {
        pool_variable_destroy(validator->pool);
    }
}

int lambda_validator_load_schema_string(LambdaValidator* validator, const char* schema_source, const char* schema_name) {
    if (!validator || !validator->internal_validator) return -1;
    
    return schema_validator_load_schema(validator->internal_validator, schema_source, schema_name);
}

int lambda_validator_load_schema_file(LambdaValidator* validator, const char* schema_path) {
    if (!validator || !schema_path) return -1;
    
    FILE* file = fopen(schema_path, "r");
    if (!file) return -1;
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Read file content
    char* content = (char*)pool_calloc(validator->pool, file_size + 1);
    size_t bytes_read = fread(content, 1, file_size, file);
    content[bytes_read] = '\0';
    fclose(file);
    
    // Extract schema name from filename
    const char* filename = strrchr(schema_path, '/');
    filename = filename ? filename + 1 : schema_path;
    
    // Remove extension
    char schema_name[256];
    strncpy(schema_name, filename, sizeof(schema_name) - 1);
    schema_name[sizeof(schema_name) - 1] = '\0';
    
    char* dot = strrchr(schema_name, '.');
    if (dot) *dot = '\0';
    
    return lambda_validator_load_schema_string(validator, content, schema_name);
}

// Helper function to convert ValidationResult to LambdaValidationResult
LambdaValidationResult* convert_validation_result(ValidationResult* internal_result, VariableMemPool* pool) {
    if (!internal_result) {
        LambdaValidationResult* result = (LambdaValidationResult*)pool_calloc(pool, sizeof(LambdaValidationResult));
        result->valid = false;
        result->error_count = 1;
        result->errors = (char**)pool_calloc(pool, 2 * sizeof(char*));
        result->errors[0] = strdup("Internal validation error");
        result->errors[1] = NULL;
        return result;
    }
    
    LambdaValidationResult* result = (LambdaValidationResult*)pool_calloc(pool, sizeof(LambdaValidationResult));
    result->valid = internal_result->valid;
    result->error_count = internal_result->error_count;
    result->warning_count = internal_result->warning_count;
    
    // Convert errors to string array
    if (result->error_count > 0) {
        result->errors = (char**)pool_calloc(pool, (result->error_count + 1) * sizeof(char*));
        ValidationError* current_error = internal_result->errors;
        int i = 0;
        
        while (current_error && i < result->error_count) {
            String* error_str = format_validation_error(current_error, pool);
            result->errors[i] = strdup(error_str->chars);
            current_error = current_error->next;
            i++;
        }
        result->errors[result->error_count] = NULL;
    } else {
        result->errors = NULL;
    }
    
    // Convert warnings to string array
    if (result->warning_count > 0) {
        result->warnings = (char**)pool_calloc(pool, (result->warning_count + 1) * sizeof(char*));
        ValidationWarning* current_warning = internal_result->warnings;
        int i = 0;
        
        while (current_warning && i < result->warning_count) {
            String* warning_str = format_validation_error(current_warning, pool);
            result->warnings[i] = strdup(warning_str->chars);
            current_warning = current_warning->next;
            i++;
        }
        result->warnings[result->warning_count] = NULL;
    } else {
        result->warnings = NULL;
    }
    
    return result;
}

LambdaValidationResult* lambda_validate_string(LambdaValidator* validator, const char* document_source, const char* schema_name) {
    if (!validator || !validator->internal_validator || !document_source || !schema_name) {
        LambdaValidationResult* result = (LambdaValidationResult*)calloc(1, sizeof(LambdaValidationResult));
        result->valid = false;
        result->error_count = 1;
        result->errors = (char**)calloc(2, sizeof(char*));
        result->errors[0] = strdup("Invalid validation parameters");
        result->errors[1] = NULL;
        return result;
    }
    
    // Parse document (this would need to be implemented based on the document format)
    // For now, create a placeholder item
    Item document_item = {.item = ITEM_NULL};
    
    ValidationResult* internal_result = validate_document(validator->internal_validator, document_item, schema_name);
    return convert_validation_result(internal_result, validator->pool);
}

LambdaValidationResult* lambda_validate_file(LambdaValidator* validator, const char* document_file, const char* schema_name) {
    if (!validator || !document_file || !schema_name) {
        LambdaValidationResult* result = (LambdaValidationResult*)calloc(1, sizeof(LambdaValidationResult));
        result->valid = false;
        result->error_count = 1;
        result->errors = (char**)calloc(2, sizeof(char*));
        result->errors[0] = strdup("Invalid parameters");
        result->errors[1] = NULL;
        return result;
    }
    
    FILE* file = fopen(document_file, "r");
    if (!file) {
        LambdaValidationResult* result = (LambdaValidationResult*)calloc(1, sizeof(LambdaValidationResult));
        result->valid = false;
        result->error_count = 1;
        result->errors = (char**)calloc(2, sizeof(char*));
        result->errors[0] = strdup("Could not open document file");
        result->errors[1] = NULL;
        return result;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Read file content
    char* content = (char*)malloc(file_size + 1);
    size_t bytes_read = fread(content, 1, file_size, file);
    content[bytes_read] = '\0';
    fclose(file);
    
    LambdaValidationResult* result = lambda_validate_string(validator, content, schema_name);
    free(content);
    
    return result;
}

void lambda_validation_result_free(LambdaValidationResult* result) {
    if (!result) return;
    
    if (result->errors) {
        for (int i = 0; i < result->error_count; i++) {
            free(result->errors[i]);
        }
        free(result->errors);
    }
    
    if (result->warnings) {
        for (int i = 0; i < result->warning_count; i++) {
            free(result->warnings[i]);
        }
        free(result->warnings);
    }
    
    free(result);
}

void lambda_validator_set_options(LambdaValidator* validator, LambdaValidationOptions* options) {
    if (!validator || !validator->internal_validator || !options) return;
    
    ValidationOptions* internal_options = &validator->internal_validator->default_options;
    internal_options->strict_mode = options->strict_mode;
    internal_options->allow_unknown_fields = options->allow_unknown_fields;
    internal_options->allow_empty_elements = options->allow_empty_elements;
    internal_options->max_depth = options->max_validation_depth;
    
    // Update context options as well
    if (validator->internal_validator->context) {
        validator->internal_validator->context->options = *internal_options;
    }
}

LambdaValidationOptions* lambda_validator_get_options(LambdaValidator* validator) {
    if (!validator || !validator->internal_validator) return NULL;
    
    ValidationOptions* internal_options = &validator->internal_validator->default_options;
    LambdaValidationOptions* options = (LambdaValidationOptions*)pool_calloc(validator->pool, sizeof(LambdaValidationOptions));
    
    options->strict_mode = internal_options->strict_mode;
    options->allow_unknown_fields = internal_options->allow_unknown_fields;
    options->allow_empty_elements = internal_options->allow_empty_elements;
    options->max_validation_depth = internal_options->max_depth;
    options->enabled_custom_rules = NULL;  // Not implemented yet
    options->disabled_rules = NULL;        // Not implemented yet
    
    return options;
}

// ==================== Utility Functions ====================

void validation_result_destroy(ValidationResult* result) {
    if (!result) return;
    
    // Note: We don't free individual error messages or path components
    // since they're allocated from the memory pool and will be freed
    // when the pool is destroyed
    
    // The ValidationResult structure itself is also allocated from
    // the memory pool, so we don't need to explicitly free it
}

ValidationResult* validate_document(SchemaValidator* validator, Item document, const char* schema_name) {
    if (!validator || !schema_name) {
        return nullptr;
    }
    
    // Look up the schema by name in the validator's C++ schema registry
    StrView lookup_name;
    lookup_name.str = schema_name;
    lookup_name.length = strlen(schema_name);
    
    SchemaEntry lookup = { .name = lookup_name };
    const SchemaEntry* found_entry = (const SchemaEntry*)hashmap_get(validator->schemas, &lookup);
    if (!found_entry) {
        // If schema not found, fall back to a basic validation
        printf("Warning: Schema '%s' not found, using basic validation\n", schema_name);
        TypeSchema* fallback_schema = create_primitive_schema(LMD_TYPE_ANY, validator->pool);
        if (!fallback_schema) {
            return nullptr;
        }
        return validate_item(validator, document, fallback_schema, validator->context);
    }
    
    // Use the actual loaded schema for validation
    return validate_item(validator, document, found_entry->schema, validator->context);
}
