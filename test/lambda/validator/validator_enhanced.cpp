/**
 * @file validator_enhanced.cpp
 * @brief Enhanced Lambda Schema Validator with Error Recovery
 * @author Henry Luo
 * @license MIT
 */

#include "validator.h"
#include <cstring>
#include <cassert>
#include <memory>
#include <string>

// ==================== Enhanced Validation Context Management ====================

ValidationContext* create_enhanced_validation_context(VariableMemPool* pool, ValidationOptions options) {
    ValidationContext* context = (ValidationContext*)pool_calloc(pool, sizeof(ValidationContext));
    if (!context) return NULL;

    context->pool = pool;
    context->path = NULL;
    context->schema_registry = NULL; // Set by caller
    context->visited = hashmap_new(
        sizeof(VisitedEntry), 16, 0, 1,
        strview_hash, strview_compare, nullptr, pool
    );
    context->custom_validators = NULL;
    context->options = options;
    context->current_depth = 0;

    return context;
}

// ==================== Enhanced Error Recovery Validation ====================

ValidationResult* validate_item_with_recovery(SchemaValidator* validator, Item item,
                                             TypeSchema* schema, ValidationContext* context) {
    if (!validator || !schema || !context) {
        return create_validation_result_with_error(context->pool,
            VALID_ERROR_PARSE_ERROR, "Invalid validation parameters", context->path);
    }

    // Initialize result for error accumulation
    ValidationResult* result = create_validation_result(context->pool);

    // Check validation depth
    if (context->current_depth >= context->options.max_depth) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_CONSTRAINT_VIOLATION, "Maximum validation depth exceeded",
            context->path, context->pool));

        // Continue validation at reduced depth if not strict mode
        if (!context->options.strict_mode) {
            context->current_depth = context->options.max_depth - 1;
        } else {
            return result;
        }
    }

    context->current_depth++;

    // Dispatch to appropriate validation function with error recovery
    ValidationResult* type_result = NULL;

    try {
        switch (schema->schema_type) {
            case LMD_SCHEMA_PRIMITIVE:
                type_result = validate_primitive_with_recovery(item, schema, context);
                break;
            case LMD_SCHEMA_UNION:
                type_result = validate_union_with_recovery(validator, item, schema, context);
                break;
            case LMD_SCHEMA_ARRAY:
                type_result = validate_array_with_recovery(validator, item, schema, context);
                break;
            case LMD_SCHEMA_MAP:
                type_result = validate_map_with_recovery(validator, item, schema, context);
                break;
            case LMD_SCHEMA_ELEMENT:
                type_result = validate_element_with_recovery(validator, item, schema, context);
                break;
            case LMD_SCHEMA_OCCURRENCE:
                type_result = validate_occurrence_with_recovery(validator, item, schema, context);
                break;
            case LMD_SCHEMA_REFERENCE:
                type_result = validate_reference_with_recovery(validator, item, schema, context);
                break;
            case LMD_SCHEMA_LITERAL:
                type_result = validate_literal_with_recovery(item, schema, context);
                break;
            default:
                type_result = create_validation_result(context->pool);
                add_validation_error(type_result, create_validation_error(
                    VALID_ERROR_TYPE_MISMATCH, "Unknown schema type",
                    context->path, context->pool));
                break;
        }
    } catch (...) {
        // C++ exception recovery
        type_result = create_validation_result(context->pool);
        add_validation_error(type_result, create_validation_error(
            VALID_ERROR_PARSE_ERROR, "Internal validation error",
            context->path, context->pool));
    }

    // Merge results
    if (type_result) {
        merge_validation_results(result, type_result);
    }

    // Run custom validators with error isolation
    if (context->custom_validators && (result->valid || !context->options.strict_mode)) {
        ValidationResult* custom_result = run_custom_validators_with_recovery(
            item, schema, context);
        if (custom_result) {
            merge_validation_results(result, custom_result);
        }
    }

    context->current_depth--;
    return result;
}

// ==================== Enhanced Primitive Validation ====================

ValidationResult* validate_primitive_with_recovery(Item item, TypeSchema* schema, ValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);

    if (schema->schema_type != LMD_SCHEMA_PRIMITIVE) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Schema is not primitive type",
            ctx->path, ctx->pool));
        return result;
    }

    SchemaPrimitive* prim_schema = (SchemaPrimitive*)schema->schema_data;
    if (!prim_schema) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_PARSE_ERROR, "Invalid primitive schema data",
            ctx->path, ctx->pool));
        return result;
    }

    TypeId expected_type = prim_schema->primitive_type;
    TypeId actual_type = get_type_id(item);

    if (!is_compatible_type(actual_type, expected_type)) {
        // Enhanced error message with type information
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg),
                "Type mismatch: expected %s, got %s",
                get_type_name(expected_type),
                get_type_name(actual_type));

        ValidationError* error = create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->path, ctx->pool);
        error->expected = schema;
        error->actual = item;

        // Add suggestions for common type conversions
        error->suggestions = generate_type_conversion_suggestions(actual_type, expected_type, ctx->pool);

        add_validation_error(result, error);
    }

    return result;
}

// ==================== Enhanced Array Validation ====================

ValidationResult* validate_array_with_recovery(SchemaValidator* validator, Item item,
                                               TypeSchema* schema, ValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);

    if (schema->schema_type != LMD_SCHEMA_ARRAY) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Schema is not array type",
            ctx->path, ctx->pool));
        return result;
    }

    TypeId actual_type = get_type_id(item);
    if (actual_type != LMD_TYPE_ARRAY && actual_type != LMD_TYPE_LIST) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "Expected array or list, got %s", get_type_name(actual_type));
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->path, ctx->pool));

        // Continue validation if not strict mode
        if (ctx->options.strict_mode) {
            return result;
        }
    }

    SchemaArray* array_schema = (SchemaArray*)schema->schema_data;
    if (!array_schema) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_PARSE_ERROR, "Invalid array schema data",
            ctx->path, ctx->pool));
        return result;
    }

    List* list = (List*)item.pointer;
    if (!list) {
        if (array_schema->occurrence == '+') {
            add_validation_error(result, create_validation_error(
                VALID_ERROR_OCCURRENCE_ERROR, "Array cannot be empty (+ occurrence)",
                ctx->path, ctx->pool));
        }
        return result;
    }

    // Check occurrence constraints
    if (array_schema->occurrence == '+' && list->length == 0) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_OCCURRENCE_ERROR, "Array cannot be empty (+ occurrence)",
            ctx->path, ctx->pool));
    }

    // Validate each element with path tracking
    if (array_schema->element_type) {
        for (long i = 0; i < list->length; i++) {
            Item element = list_get(list, i);

            // Push array index to path
            PathSegment* index_path = create_index_path(i, ctx->pool);
            PathSegment* old_path = push_path_segment(ctx, index_path);

            // Validate element with recovery
            ValidationResult* element_result = validate_item_with_recovery(
                validator, element, array_schema->element_type, ctx);

            // Merge results and continue even if element fails
            if (element_result) {
                merge_validation_results(result, element_result);
            }

            // Pop path
            pop_path_segment(ctx);

            // Early exit only in strict mode with critical errors
            if (ctx->options.strict_mode && element_result && !element_result->valid &&
                has_critical_errors(element_result)) {
                break;
            }
        }
    }

    return result;
}

// ==================== Enhanced Map Validation ====================

ValidationResult* validate_map_with_recovery(SchemaValidator* validator, Item item,
                                             TypeSchema* schema, ValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);

    if (schema->schema_type != LMD_SCHEMA_MAP) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Schema is not map type",
            ctx->path, ctx->pool));
        return result;
    }

    TypeId actual_type = get_type_id(item);
    if (actual_type != LMD_TYPE_MAP && actual_type != LMD_TYPE_ELEMENT) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "Expected map or element, got %s", get_type_name(actual_type));
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->path, ctx->pool));

        if (ctx->options.strict_mode) {
            return result;
        }
    }

    SchemaMap* map_schema = (SchemaMap*)schema->schema_data;
    if (!map_schema) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_PARSE_ERROR, "Invalid map schema data",
            ctx->path, ctx->pool));
        return result;
    }

    Map* map = (Map*)item.pointer;
    if (!map) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Map is null",
            ctx->path, ctx->pool));
        return result;
    }

    // Track which required fields we've seen
    List* required_fields = list_new(ctx->pool);
    List* found_fields = list_new(ctx->pool);

    // Collect required fields
    SchemaMapField* field = map_schema->fields;
    while (field) {
        if (field->required) {
            list_add(required_fields, (Item){.item = LMD_TYPE_STRING,
                .pointer = string_from_strview(field->name, ctx->pool)});
        }
        field = field->next;
    }

    // Validate each field in the map
    void* iter = map_iterator_begin(map);
    while (iter) {
        Item key_item, value_item;
        if (map_iterator_get(iter, &key_item, &value_item)) {
            if (get_type_id(key_item) == LMD_TYPE_STRING) {
                String* key_str = (String*)key_item.pointer;
                StrView key_view = {.str = key_str->chars, .length = key_str->len};

                // Find field schema
                SchemaMapField* field_schema = find_map_field(map_schema, key_view);
                if (field_schema) {
                    // Track found field
                    list_add(found_fields, key_item);

                    // Push field path
                    PathSegment* field_path = create_field_path(key_str->chars, ctx->pool);
                    push_path_segment(ctx, field_path);

                    // Validate field value
                    ValidationResult* field_result = validate_item_with_recovery(
                        validator, value_item, field_schema->type, ctx);

                    if (field_result) {
                        merge_validation_results(result, field_result);
                    }

                    // Pop path
                    pop_path_segment(ctx);
                } else if (!map_schema->is_open && !ctx->options.allow_unknown_fields) {
                    // Unknown field error
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg),
                            "Unknown field '%s'", key_str->chars);

                    PathSegment* field_path = create_field_path(key_str->chars, ctx->pool);
                    push_path_segment(ctx, field_path);

                    ValidationError* error = create_validation_error(
                        VALID_ERROR_UNEXPECTED_FIELD, error_msg, ctx->path, ctx->pool);

                    // Add field name suggestions
                    error->suggestions = suggest_similar_field_names(
                        key_str->chars, map_schema, ctx->pool);

                    add_validation_error(result, error);
                    pop_path_segment(ctx);
                }
            }
        }
        iter = map_iterator_next(map, iter);
    }

    // Check for missing required fields
    for (long i = 0; i < required_fields->length; i++) {
        Item required_field = list_get(required_fields, i);
        String* req_str = (String*)required_field.pointer;

        bool found = false;
        for (long j = 0; j < found_fields->length; j++) {
            Item found_field = list_get(found_fields, j);
            String* found_str = (String*)found_field.pointer;
            if (strcmp(req_str->chars, found_str->chars) == 0) {
                found = true;
                break;
            }
        }

        if (!found) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg),
                    "Missing required field '%s'", req_str->chars);

            PathSegment* field_path = create_field_path(req_str->chars, ctx->pool);
            push_path_segment(ctx, field_path);

            add_validation_error(result, create_validation_error(
                VALID_ERROR_MISSING_FIELD, error_msg, ctx->path, ctx->pool));

            pop_path_segment(ctx);
        }
    }

    return result;
}

// ==================== Enhanced Union Validation ====================

ValidationResult* validate_union_with_recovery(SchemaValidator* validator, Item item,
                                               TypeSchema* schema, ValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);

    if (schema->schema_type != LMD_SCHEMA_UNION) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Schema is not union type",
            ctx->path, ctx->pool));
        return result;
    }

    SchemaUnion* union_schema = (SchemaUnion*)schema->schema_data;
    if (!union_schema || union_schema->type_count == 0) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_PARSE_ERROR, "Invalid union schema",
            ctx->path, ctx->pool));
        return result;
    }

    // Try to validate against each type in the union
    List* union_errors = list_new(ctx->pool);
    bool any_valid = false;

    for (int i = 0; i < union_schema->type_count; i++) {
        TypeSchema* union_type = union_schema->types[i];
        if (!union_type) continue;

        // Create isolated validation context to avoid path contamination
        ValidationResult* union_result = validate_item_with_recovery(
            validator, item, union_type, ctx);

        if (union_result && union_result->valid) {
            any_valid = true;
            // For unions, we only need one type to match
            merge_validation_results(result, union_result);
            break;
        } else if (union_result) {
            // Store error for potential reporting
            list_add(union_errors, (Item){.item = LMD_SCHEMA_PRIMITIVE, .pointer = union_result});
        }
    }

    // If no types matched, add comprehensive error
    if (!any_valid) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg),
                "Value does not match any type in union (%d types tried)",
                union_schema->type_count);

        ValidationError* error = create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->path, ctx->pool);

        // Add suggestions based on closest match
        error->suggestions = generate_union_type_suggestions(item, union_schema, ctx->pool);

        add_validation_error(result, error);

        // In non-strict mode, include details about why each union type failed
        if (!ctx->options.strict_mode) {
            for (long i = 0; i < union_errors->length; i++) {
                ValidationResult* union_error_result = (ValidationResult*)list_get(union_errors, i).pointer;
                if (union_error_result) {
                    merge_validation_results(result, union_error_result);
                }
            }
        }
    }

    return result;
}

// ==================== Enhanced Error Reporting Utilities ====================

ValidationResult* create_validation_result_with_error(VariableMemPool* pool,
                                                      ValidationErrorCode code,
                                                      const char* message,
                                                      PathSegment* path) {
    ValidationResult* result = create_validation_result(pool);
    ValidationError* error = create_validation_error(code, message, path, pool);
    add_validation_error(result, error);
    return result;
}

bool has_critical_errors(ValidationResult* result) {
    if (!result || !result->errors) return false;

    ValidationError* error = result->errors;
    while (error) {
        switch (error->code) {
            case VALID_ERROR_PARSE_ERROR:
            case VALID_ERROR_CIRCULAR_REFERENCE:
                return true;
            default:
                break;
        }
        error = error->next;
    }
    return false;
}

List* generate_type_conversion_suggestions(TypeId actual, TypeId expected, VariableMemPool* pool) {
    List* suggestions = list_new(pool);

    // Simple type conversion suggestions
    if (actual == LMD_TYPE_STRING && expected == LMD_TYPE_INT) {
        list_add(suggestions, (Item){.item = LMD_TYPE_STRING,
            .pointer = string_from_strview(strview_from_cstr("Convert string to integer"), pool)});
    } else if (actual == LMD_TYPE_INT && expected == LMD_TYPE_STRING) {
        list_add(suggestions, (Item){.item = LMD_TYPE_STRING,
            .pointer = string_from_strview(strview_from_cstr("Convert integer to string"), pool)});
    } else if (actual == LMD_TYPE_FLOAT && expected == LMD_TYPE_INT) {
        list_add(suggestions, (Item){.item = LMD_TYPE_STRING,
            .pointer = string_from_strview(strview_from_cstr("Round float to integer"), pool)});
    }

    return suggestions;
}

List* suggest_similar_field_names(const char* field_name, SchemaMap* map_schema, VariableMemPool* pool) {
    List* suggestions = list_new(pool);

    if (!field_name || !map_schema) return suggestions;

    SchemaMapField* field = map_schema->fields;
    while (field) {
        // Simple similarity check
        if (field->name.length > 0 && field->name.str) {
            int distance = calculate_edit_distance(field_name, field->name.str, field->name.length);
            if (distance <= 2) {  // Allow up to 2 character differences
                String* suggestion = string_from_strview(field->name, pool);
                list_add(suggestions, (Item){.item = LMD_TYPE_STRING, .pointer = suggestion});
            }
        }
        field = field->next;
    }

    return suggestions;
}

int calculate_edit_distance(const char* s1, const char* s2, size_t s2_len) {
    // Simple Levenshtein distance calculation
    size_t s1_len = strlen(s1);
    if (s1_len == 0) return (int)s2_len;
    if (s2_len == 0) return (int)s1_len;

    // For simplicity, use a basic comparison
    int differences = abs((int)s1_len - (int)s2_len);
    size_t min_len = (s1_len < s2_len) ? s1_len : s2_len;

    for (size_t i = 0; i < min_len; i++) {
        if (s1[i] != s2[i]) {
            differences++;
        }
    }

    return differences;
}

const char* get_type_name(TypeId type_id) {
    switch (type_id) {
        case LMD_TYPE_INT: return "int";
        case LMD_TYPE_FLOAT: return "float";
        case LMD_TYPE_STRING: return "string";
        case LMD_TYPE_BOOL: return "bool";
        case LMD_TYPE_ARRAY: return "array";
        case LMD_TYPE_LIST: return "list";
        case LMD_TYPE_MAP: return "map";
        case LMD_TYPE_ELEMENT: return "element";
        case ITEM_NULL: return "null";
        default: return "unknown";
    }
}

// ==================== Enhanced Path Management ====================

String* format_validation_path_enhanced(PathSegment* path, VariableMemPool* pool) {
    if (!path) {
        return string_from_strview(strview_from_cstr("(root)"), pool);
    }

    // Calculate total length needed with enhanced formatting
    size_t total_len = 0;
    int segment_count = 0;
    PathSegment* current = path;

    while (current && segment_count < 100) {
        switch (current->type) {
            case PATH_FIELD:
                total_len += current->data.field_name.length + 1; // +1 for dot
                break;
            case PATH_INDEX:
                total_len += 20; // Enough for [<large_number>]
                break;
            case PATH_ELEMENT:
                total_len += current->data.element_tag.length + 2; // +2 for <>
                break;
            case PATH_ATTRIBUTE:
                total_len += current->data.attr_name.length + 1; // +1 for @
                break;
        }
        segment_count++;
        current = current->next;
    }

    // Build path string
    char* buffer = (char*)pool_calloc(pool, total_len + 10); // +10 for safety
    char* pos = buffer;

    // Build segments in reverse order
    PathSegment* segments[100];
    current = path;
    int count = 0;

    while (current && count < 100) {
        segments[count++] = current;
        current = current->next;
    }

    // Build path from root to leaf
    for (int i = count - 1; i >= 0; i--) {
        PathSegment* segment = segments[i];
        switch (segment->type) {
            case PATH_FIELD:
                if (i < count - 1) *pos++ = '.';  // Don't add dot at beginning
                memcpy(pos, segment->data.field_name.str, segment->data.field_name.length);
                pos += segment->data.field_name.length;
                break;
            case PATH_INDEX:
                snprintf(pos, 20, "[%ld]", segment->data.index);
                pos += strlen(pos);
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
