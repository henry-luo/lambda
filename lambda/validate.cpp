#include "validator.hpp"

ValidationResult* validate_against_primitive_type(AstValidator* validator, TypedItem item, Type* type) {
    log_debug("[AST_VALIDATOR] Validating primitive: expected=%d, actual=%d", type->type_id, item.type_id);
    ValidationResult* result = create_validation_result(validator->pool);
    // todo: match literal values
    if (type->type_id == item.type_id) {
        result->valid = true;
    } else {
        result->valid = false;
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "Type mismatch: expected %s, got type %d",
                type_to_string(type), item.type_id);

        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->current_path, validator->pool);
        if (error) {
            error->expected = type;
            error->actual = (Item){.pointer = (uint64_t)(uintptr_t)item.pointer, .type_id = item.type_id};
            add_validation_error(result, error);
        }
    }
    return result;
}

ValidationResult* validate_against_base_type(AstValidator* validator, TypedItem item, TypeType* type) {
    ValidationResult* result = create_validation_result(validator->pool);
    Type *base_type = type->type;
    log_debug("[AST_VALIDATOR] Validating base type: expected=%d, actual=%d", base_type->type_id, item.type_id);
    if (LMD_TYPE_INT <= base_type->type_id && base_type->type_id <= LMD_TYPE_NUMBER) {
        // number promotion - allow int/float/decimal interchangeably
        if (LMD_TYPE_INT <= item.type_id && item.type_id <= base_type->type_id) {
            result->valid = true;
        }
        else { result->valid = false; }
    }
    else if (base_type->type_id == item.type_id) {
        result->valid = true;
    } else {
        result->valid = false;
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "Type mismatch: expected %s, got type %d",
                type_to_string(base_type), item.type_id);

        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->current_path, validator->pool);
        if (error) {
            error->expected = base_type;
            error->actual = (Item){.pointer = (uint64_t)(uintptr_t)item.pointer, .type_id = item.type_id};
            add_validation_error(result, error);
        }
    }
    return result;
}

ValidationResult* validate_against_array_type(AstValidator* validator, TypedItem item, TypeArray* array_type) {
    log_debug("Validating array type");
    ValidationResult* result = create_validation_result(validator->pool);

    // Check if item is actually an array/list
    if (item.type_id != LMD_TYPE_ARRAY && item.type_id != LMD_TYPE_LIST && item.type_id != LMD_TYPE_RANGE &&
        item.type_id != LMD_TYPE_ARRAY_INT && item.type_id != LMD_TYPE_ARRAY_INT64 && item.type_id != LMD_TYPE_ARRAY_FLOAT) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
            "Type mismatch: expected array, got type %d", item.type_id);
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->current_path, validator->pool));
        return result;
    }

    // Get the actual array data
    List* array_data = item.list;
    if (!array_data) {
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Array data is null", validator->current_path, validator->pool));
        return result;
    }

    // Validate array length if specified
    // if (array_type->length >= 0 && array_data->length != array_type->length) {
    //     char error_msg[256];
    //     snprintf(error_msg, sizeof(error_msg),
    //             "Array length mismatch: expected %ld, got %ld",
    //             array_type->length, array_data->length);
    //     add_validation_error(result, create_validation_error(
    //         AST_VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, validator->current_path, validator->pool));
    // }

    PathSegment* pa_path = validator->current_path;
    // Validate each array element against nested type
    log_debug("validating array length: %ld", array_data->length);
    if (array_type->nested && array_data->length > 0) {
        for (long i = 0; i < array_data->length; i++) {
            // Create path segment for array index
            PathSegment* path = nullptr;
            if (validator->pool) {
                path = (PathSegment*)pool_calloc(validator->pool, sizeof(PathSegment));
                if (path) {
                    path->type = PATH_INDEX;
                    path->data.index = i;
                    path->next = validator->current_path;
                }
            }
            validator->current_path = path;

            // Get array element (convert Item to TypedItem)
            TypedItem array_item;
            switch (item.type_id) {
                case LMD_TYPE_ARRAY:
                    array_item = list_get_typed(array_data, i);
                    break;
                case LMD_TYPE_ARRAY_INT: {
                    ArrayInt* array_int = (ArrayInt*)item.array;
                    array_item = (TypedItem){.type_id = LMD_TYPE_INT, .int_val = array_int->items[i]};
                    break;
                }
                case LMD_TYPE_ARRAY_INT64: {
                    ArrayInt64* array_int64 = (ArrayInt64*)item.array;
                    array_item = (TypedItem){.type_id = LMD_TYPE_INT64, .long_val = array_int64->items[i]};
                    break;
                }
                case LMD_TYPE_ARRAY_FLOAT: {
                    ArrayFloat* array_float = (ArrayFloat*)item.array;
                    array_item = (TypedItem){.type_id = LMD_TYPE_FLOAT, .double_val = array_float->items[i]};
                    break;
                }
                case LMD_TYPE_RANGE:
                    array_item = (TypedItem){.type_id = LMD_TYPE_INT, .int_val = (int)(item.range->start + i)};
                    break;
                case LMD_TYPE_LIST:
                    array_item = list_get_typed(array_data, i);
                    break;
            }

            // Recursively validate element
            log_debug("validating array item at index %ld, type %d", i, array_item.type_id);
            ValidationResult* item_result = validate_against_type(
                validator, array_item, array_type->nested);

            // Merge element validation results
            if (item_result && !item_result->valid) {
                result->valid = false;
                // Add all element errors to main result
                ValidationError* element_error = item_result->errors;
                while (element_error) {
                    result->error_count++;
                    // Copy error to main result
                    ValidationError* copied_error = create_validation_error(
                        element_error->code, element_error->message->chars,
                        element_error->path, validator->pool);
                    add_validation_error(result, copied_error);
                    element_error = element_error->next;
                }
            }
        }
    }
    validator->current_path = pa_path;
    return result;
}

ValidationResult* validate_against_map_type(AstValidator* validator, TypedItem item, TypeMap* map_type) {
    ValidationResult* result = create_validation_result(validator->pool);

    // Check if item is actually a map
    if (item.type_id != LMD_TYPE_MAP) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "Type mismatch: expected map, got type %d", item.type_id);
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->current_path, validator->pool));
        return result;
    }

    // Get the actual map data (assuming it's stored as a struct with typed fields)
    void* map_data = item.pointer;
    if (!map_data) {
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Map data is null", validator->current_path, validator->pool));
        return result;
    }

    PathSegment* pa_path = validator->current_path;
    // Validate each field in the map shape
    ShapeEntry* shape_entry = map_type->shape;
    while (shape_entry) {
        // Create path segment for map field
        PathSegment* field_path = nullptr;
        if (validator->pool) {
            field_path = (PathSegment*)pool_calloc(validator->pool, sizeof(PathSegment));
            if (field_path) {
                field_path->type = PATH_FIELD;
                field_path->data.field_name = *shape_entry->name;
                field_path->next = validator->current_path;
            }
        }
        validator->current_path = field_path;

        // Extract field value from map data using byte offset
        void* field_data = (char*)map_data + shape_entry->byte_offset;

        // Create TypedItem for the field (this is simplified - would need proper type detection)
        TypedItem field_item;
        field_item.type_id = shape_entry->type->type_id;
        field_item.pointer = field_data;

        // Recursively validate field
        ValidationResult* field_result = validate_against_type(
            validator, field_item, shape_entry->type);

        // Merge field validation results
        if (field_result && !field_result->valid) {
            result->valid = false;

            // Add all field errors to main result
            ValidationError* field_error = field_result->errors;
            while (field_error) {
                result->error_count++;

                // Copy error to main result
                ValidationError* copied_error = create_validation_error(
                    field_error->code, field_error->message->chars,
                    field_error->path, validator->pool);
                add_validation_error(result, copied_error);

                field_error = field_error->next;
            }
        }
        shape_entry = shape_entry->next;
    }
    validator->current_path = pa_path;
    return result;
}

ValidationResult* validate_against_element_type(AstValidator* validator, TypedItem item, TypeElmt* element_type) {
    ValidationResult* result = create_validation_result(validator->pool);

    // Check if item is actually an element
    if (item.type_id != LMD_TYPE_ELEMENT) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "Type mismatch: expected element, got type %d", item.type_id);
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->current_path, validator->pool));
        return result;
    }

    // Get the actual element data
    Element* element_data = (Element*)item.pointer;
    if (!element_data) {
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Element data is null", validator->current_path, validator->pool));
        return result;
    }

    // Validate element name if specified
    PathSegment* pa_path = validator->current_path;
    if (element_type->name.length > 0) {
        // Create path segment for element name
        PathSegment* element_name_path = nullptr;
        if (validator->pool) {
            element_name_path = (PathSegment*)pool_calloc(validator->pool, sizeof(PathSegment));
            if (element_name_path) {
                element_name_path->type = PATH_ELEMENT;
                element_name_path->data.element_tag = element_type->name;
                element_name_path->next = validator->current_path;
            }
        }
        // Create context for attribute validation
        validator->current_path = element_name_path;
        // Validate element name
        printf("[AST_VALIDATOR] Element name validation for: %.*s\n",
                (int)element_type->name.length, element_type->name.str);
    }

    // TypeElmt inherits from TypeMap, so we can validate attributes as map fields
    TypeMap* map_part = (TypeMap*)element_type;
    if (map_part->shape) {
        // Create a TypedItem for the element's attribute data
        TypedItem attr_item;
        attr_item.type_id = LMD_TYPE_MAP;
        attr_item.pointer = element_data->data;

        // Create path segment for attributes
        PathSegment* attr_path = nullptr;
        if (validator->pool) {
            attr_path = (PathSegment*)pool_calloc(validator->pool, sizeof(PathSegment));
            if (attr_path) {
                attr_path->type = PATH_ATTRIBUTE;
                attr_path->data.attr_name = (StrView){"attrs", 5};
                attr_path->next = validator->current_path;
            }
        }
        validator->current_path = attr_path;

        // Validate attributes using map validation
        ValidationResult* attr_result = validate_against_map_type(validator, attr_item, map_part);

        // Merge attribute validation results
        if (attr_result && !attr_result->valid) {
            result->valid = false;
            result->error_count += attr_result->error_count;

            ValidationError* attr_error = attr_result->errors;
            while (attr_error) {
                ValidationError* copied_error = create_validation_error(
                    attr_error->code, attr_error->message->chars,
                    attr_error->path, validator->pool);
                add_validation_error(result, copied_error);
                attr_error = attr_error->next;
            }
        }
    }

    // Validate element content length
    if (element_type->content_length > 0) {
        // Create path segment for content
        PathSegment* content_path = nullptr;
        if (validator->pool) {
            content_path = (PathSegment*)pool_calloc(validator->pool, sizeof(PathSegment));
            if (content_path) {
                content_path->type = PATH_ELEMENT;
                content_path->data.element_tag = (StrView){"content", 7};
                content_path->next = validator->current_path;
            }
        }

        if (element_data->length > element_type->content_length) {
            char content_error[256];
            snprintf(content_error, sizeof(content_error),
                    "Element content length mismatch: expected %ld, got %ld",
                    element_type->content_length, element_data->length);
            add_validation_error(result, create_validation_error(
                AST_VALID_ERROR_CONSTRAINT_VIOLATION, content_error, content_path, validator->pool));
        }
    }
    validator->current_path = pa_path;
    return result;
}

ValidationResult* validate_against_union_type(AstValidator* validator, TypedItem item, Type** union_types, int type_count) {
    ValidationResult* result = create_validation_result(validator->pool);

    if (!union_types || type_count <= 0) {
        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Invalid union type definition", validator->current_path, validator->pool);
        add_validation_error(result, error);
        return result;
    }

    // Try to validate against each type in the union
    bool any_valid = false;
    ValidationResult* best_result = nullptr;
    int min_errors = 2147483647; // INT_MAX value

    for (int i = 0; i < type_count; i++) {
        if (!union_types[i]) continue;

        // Create path segment for union member
        PathSegment* union_path = nullptr;
        if (validator->pool) {
            union_path = (PathSegment*)pool_calloc(validator->pool, sizeof(PathSegment));
            if (union_path) {
                union_path->type = PATH_INDEX;
                union_path->data.index = i;
                union_path->next = validator->current_path;
            }
        }
        PathSegment* pa_path = validator->current_path;
        validator->current_path = union_path;

        // Try validating against this union member
        ValidationResult* member_result = validate_against_type(validator, item, union_types[i]);
        if (member_result && member_result->valid) {
            any_valid = true;
            result->valid = true;
            return result;
        } else if (member_result && member_result->error_count < min_errors) {
            min_errors = member_result->error_count;
            best_result = member_result;
        }

        validator->current_path = pa_path;
    }

    // If no type in the union was valid, report the best error
    if (!any_valid) {
        result->valid = false;
        if (best_result) {
            result->error_count = best_result->error_count;
            ValidationError* error = best_result->errors;
            while (error) {
                ValidationError* copied_error = create_validation_error(
                    error->code, error->message->chars, error->path, validator->pool);
                add_validation_error(result, copied_error);
                error = error->next;
            }
        } else {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg),
                    "Item does not match any type in union (%d types)", type_count);
            ValidationError* error = create_validation_error(
                AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->current_path, validator->pool);
            add_validation_error(result, error);
        }
    }

    return result;
}

ValidationResult* validate_against_occurrence(AstValidator* validator, TypedItem* items, long item_count, Type* expected_type, Operator occurrence_op) {
    ValidationResult* result = create_validation_result(validator->pool);

    if (!expected_type) {
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Invalid occurrence constraint parameters", validator->current_path, validator->pool));
        return result;
    }

    // Validate occurrence constraints based on operator
    switch (occurrence_op) {
        case OPERATOR_OPTIONAL: // ? (0 or 1)
            if (item_count > 1) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                        "Optional constraint violated: expected 0 or 1 items, got %ld", item_count);
                add_validation_error(result, create_validation_error(
                    AST_VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, validator->current_path, validator->pool));
            }
            break;

        case OPERATOR_ONE_MORE: // + (1 or more)
            if (item_count < 1) {
                add_validation_error(result, create_validation_error(
                    AST_VALID_ERROR_CONSTRAINT_VIOLATION,
                    "One-or-more constraint violated: expected at least 1 item, got 0",
                    validator->current_path, validator->pool));
            }
            break;

        case OPERATOR_ZERO_MORE: // * (0 or more)
            // Always valid for zero-or-more
            break;

        default:
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg),
                    "Unsupported occurrence operator: %d", occurrence_op);
            add_validation_error(result, create_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, error_msg, validator->current_path, validator->pool));
            return result;
    }

    // Validate each item against the expected type
    for (long i = 0; i < item_count; i++) {
        // Create path segment for item index
        PathSegment* item_path = nullptr;
        if (validator->pool) {
            item_path = (PathSegment*)pool_calloc(validator->pool, sizeof(PathSegment));
            if (item_path) {
                item_path->type = PATH_INDEX;
                item_path->data.index = i;
                item_path->next = validator->current_path;
            }
        }
        PathSegment* pa_path = validator->current_path;
        validator->current_path = item_path;
        validator->current_depth++;

        ValidationResult* item_result = validate_against_type(validator, items[i], expected_type);
        if (item_result && !item_result->valid) {
            result->valid = false;
            result->error_count += item_result->error_count;

            ValidationError* item_error = item_result->errors;
            while (item_error) {
                ValidationError* copied_error = create_validation_error(
                    item_error->code, item_error->message->chars,
                    item_error->path, validator->pool);
                add_validation_error(result, copied_error);
                item_error = item_error->next;
            }
        }
        validator->current_path = pa_path;  validator->current_depth--;
    }

    return result;
}

/*
ValidationResult* validate_against_reference(SchemaValidator* validator, TypedItem typed_item, Type* schema, AstValidationContext* ctx) {
    ////if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: Starting reference validation for '%.*s'\n",
    //       (int)schema->name.length, schema->name.str);
    ValidationResult* result = create_validation_result(ctx->pool);

    if (schema->schema_type != LMD_SCHEMA_REFERENCE) {
        add_validation_error(result, create_validation_error(
            VALID_ERROR_TYPE_MISMATCH, "Expected reference schema",
            ctx->path, ctx->pool));
        return result;
    }

    // Check for circular references FIRST
    VisitedEntry lookup = { .key = schema->name, .visited = false };
    const VisitedEntry* visited_entry = (const VisitedEntry*)hashmap_get(ctx->visited_nodes, &lookup);
    if (visited_entry && visited_entry->visited) {
        ////if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: Circular reference detected for '%.*s'\n",
        //       (int)schema->name.length, schema->name.str);
        add_validation_error(result, create_validation_error(
            VALID_ERROR_CIRCULAR_REFERENCE, "Circular type reference detected",
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

    // if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG] validate_reference: Resolved '%.*s' to schema type %d\n",
    //        (int)schema->name.length, schema->name.str, resolved->schema_type);

    // Mark as visited and validate
    VisitedEntry entry = { .key = schema->name, .visited = true };
    hashmap_set(ctx->visited_nodes, &entry);

    validation_result_destroy(result);

    result = validate_item(validator, typed_item, resolved, ctx);

    // Unmark as visited
    VisitedEntry unmark_entry = { .key = schema->name, .visited = false };
    hashmap_set(ctx->visited_nodes, &unmark_entry);

    ////if (ENABLE_SCHEMA_DEBUG) printf("[DEBUG].*: Finished validating '%.*s'\n",
    //       (int)schema->name.length, schema->name.str);
    return result;
}
*/

ValidationResult* validate_against_type(AstValidator* validator, TypedItem item, Type* type) {
    if (!validator || !type) {
        ValidationResult* result = create_validation_result(validator ? validator->pool : nullptr);
        if (result) {
            ValidationError* error = create_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, "Invalid validation parameters",
                nullptr, validator ? validator->pool : nullptr);
            add_validation_error(result, error);
        }
        return result;
    }
    // check validation depth
    if (validator->current_depth >= validator->options.max_depth) {
        ValidationResult* result = create_validation_result(validator->pool);
        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_CONSTRAINT_VIOLATION, "Maximum validation depth exceeded",
            validator->current_path, validator->pool);
        add_validation_error(result, error);
        return result;
    }

    ValidationResult* result = nullptr;
    validator->current_depth++;
    log_debug("[AST_VALIDATOR] Validating against type_id: %d", type->type_id);
    switch (type->type_id) {
        case LMD_TYPE_STRING:
        case LMD_TYPE_INT:
        case LMD_TYPE_FLOAT:
        case LMD_TYPE_BOOL:
        case LMD_TYPE_NULL:
            result = validate_against_primitive_type(validator, item, type);
            break;
        case LMD_TYPE_ARRAY:  case LMD_TYPE_LIST:
            result = validate_against_array_type(validator, item, (TypeArray*)type);
            break;
        case LMD_TYPE_MAP:
            return validate_against_map_type(validator, item, (TypeMap*)type);
        case LMD_TYPE_ELEMENT:
            return validate_against_element_type(validator, item, (TypeElmt*)type);
        case LMD_TYPE_TYPE:
            return validate_against_base_type(validator, item, (TypeType*)type);
        default:
            result = create_validation_result(validator->pool);
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg),
                    "Unsupported type for validation: %d", type->type_id);
            add_validation_error(result, create_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, error_msg, validator->current_path, validator->pool));
            break;
    }
    validator->current_depth--;
    return result;
}
