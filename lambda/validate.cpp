#include "validator.hpp"

ValidationResult* validate_against_type(AstValidator* validator, TypedItem item, Type* type, AstValidationContext* ctx) {
    if (!validator || !type || !ctx) {
        ValidationResult* result = create_validation_result(ctx ? ctx->pool : nullptr);
        if (result) {
            ValidationError* error = create_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, "Invalid validation parameters", 
                ctx ? ctx->current_path : nullptr, ctx ? ctx->pool : nullptr);
            add_validation_error(result, error);
        }
        return result;
    }
    
    // Check validation depth
    if (ctx->current_depth >= ctx->options.max_depth) {
        ValidationResult* result = create_validation_result(ctx->pool);
        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_CONSTRAINT_VIOLATION, "Maximum validation depth exceeded", 
            ctx->current_path, ctx->pool);
        add_validation_error(result, error);
        return result;
    }
    
    ctx->current_depth++;
    
    ValidationResult* result = nullptr;
    
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
            result = create_validation_result(ctx->pool);
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Unsupported type for validation: %d", type->type_id);
            add_validation_error(result, create_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, error_msg, ctx->current_path, ctx->pool));
            break;
    }
    
    ctx->current_depth--;
    return result;
}

ValidationResult* validate_against_primitive_type(TypedItem item, Type* type, AstValidationContext* ctx) {
    printf("[AST_VALIDATOR] Validating primitive: expected=%d, actual=%d\n", type->type_id, item.type_id);
    
    ValidationResult* result = create_validation_result(ctx->pool);
    
    if (type->type_id == item.type_id) {
        result->valid = true;
    } else {
        result->valid = false;
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: expected %s, got type %d",
                type_to_string(type), item.type_id);
        
        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->current_path, ctx->pool);
        if (error) {
            error->expected = type;
            error->actual = (Item){.pointer = (uint64_t)(uintptr_t)item.pointer, .type_id = item.type_id};
            add_validation_error(result, error);
        }
    }
    
    return result;
}

ValidationResult* validate_against_array_type(AstValidator* validator, TypedItem item, TypeArray* array_type, AstValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);
    
    // Check if item is actually an array/list
    if (item.type_id != LMD_TYPE_ARRAY && item.type_id != LMD_TYPE_LIST) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: expected array, got type %d", item.type_id);
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->current_path, ctx->pool));
        return result;
    }
    
    // Get the actual array data
    List* array_data = (List*)item.pointer;
    if (!array_data) {
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Array data is null", ctx->current_path, ctx->pool));
        return result;
    }
    
    // Validate array length if specified
    if (array_type->length >= 0 && array_data->length != array_type->length) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Array length mismatch: expected %ld, got %ld", 
                array_type->length, array_data->length);
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, ctx->current_path, ctx->pool));
    }
    
    // Validate each array element against nested type
    if (array_type->nested && array_data->length > 0) {
        for (long i = 0; i < array_data->length; i++) {
            // Create path segment for array index
            PathSegment* element_path = nullptr;
            if (ctx->pool) {
                element_path = (PathSegment*)pool_calloc(ctx->pool, sizeof(PathSegment));
                if (element_path) {
                    element_path->type = PATH_INDEX;
                    element_path->data.index = i;
                    element_path->next = ctx->current_path;
                }
            }
            AstValidationContext element_ctx = *ctx;
            element_ctx.current_path = element_path;
            
            // Get array element (convert Item to TypedItem)
            Item element = array_data->items[i];
            TypedItem element_item;
            element_item.type_id = (TypeId)element.type_id;
            element_item.pointer = (void*)(uintptr_t)element.pointer;
            
            // Recursively validate element
            ValidationResult* element_result = validate_against_type(
                validator, element_item, array_type->nested, &element_ctx);
            
            // Merge element validation results
            if (element_result && !element_result->valid) {
                result->valid = false;
                
                // Add all element errors to main result
                ValidationError* element_error = element_result->errors;
                while (element_error) {
                    result->error_count++;
                    
                    // Copy error to main result
                    ValidationError* copied_error = create_validation_error(
                        element_error->code, element_error->message->chars, 
                        element_error->path, ctx->pool);
                    add_validation_error(result, copied_error);
                    
                    element_error = element_error->next;
                }
            }
        }
    }
    
    return result;
}

ValidationResult* validate_against_map_type(AstValidator* validator, TypedItem item, TypeMap* map_type, AstValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);
    
    // Check if item is actually a map
    if (item.type_id != LMD_TYPE_MAP) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: expected map, got type %d", item.type_id);
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->current_path, ctx->pool));
        return result;
    }

    // Get the actual map data (assuming it's stored as a struct with typed fields)
    void* map_data = item.pointer;
    if (!map_data) {
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Map data is null", ctx->current_path, ctx->pool));
        return result;
    }
    
    // Validate each field in the map shape
    ShapeEntry* shape_entry = map_type->shape;
    while (shape_entry) {
        // Create path segment for map field
        PathSegment* field_path = nullptr;
        if (ctx->pool) {
            field_path = (PathSegment*)pool_calloc(ctx->pool, sizeof(PathSegment));
            if (field_path) {
                field_path->type = PATH_FIELD;
                field_path->data.field_name = *shape_entry->name;
                field_path->next = ctx->current_path;
            }
        }
        
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
        ValidationResult* field_result = validate_against_type(
            validator, field_item, shape_entry->type, &field_ctx);
        
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
                    field_error->path, ctx->pool);
                add_validation_error(result, copied_error);
                
                field_error = field_error->next;
            }
        }
        
        shape_entry = shape_entry->next;
    }
    
    return result;
}

ValidationResult* validate_against_element_type(AstValidator* validator, TypedItem item, TypeElmt* element_type, AstValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);
    
    // Check if item is actually an element
    if (item.type_id != LMD_TYPE_ELEMENT) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), 
                "Type mismatch: expected element, got type %d", item.type_id);
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->current_path, ctx->pool));
        return result;
    }
    
    // Get the actual element data
    Element* element_data = (Element*)item.pointer;
    if (!element_data) {
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Element data is null", ctx->current_path, ctx->pool));
        return result;
    }
    
    // Validate element name if specified
    if (element_type->name.length > 0) {
        // Create path segment for element name
        PathSegment* element_name_path = nullptr;
        if (ctx->pool) {
            element_name_path = (PathSegment*)pool_calloc(ctx->pool, sizeof(PathSegment));
            if (element_name_path) {
                element_name_path->type = PATH_ELEMENT;
                element_name_path->data.element_tag = element_type->name;
                element_name_path->next = ctx->current_path;
            }
        }
        
        // Create context for attribute validation
        AstValidationContext attr_ctx = *ctx;
        attr_ctx.current_path = element_name_path;
        
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
        if (ctx->pool) {
            attr_path = (PathSegment*)pool_calloc(ctx->pool, sizeof(PathSegment));
            if (attr_path) {
                attr_path->type = PATH_ATTRIBUTE;
                attr_path->data.attr_name = (StrView){"attrs", 5};
                attr_path->next = ctx->current_path;
            }
        }
        AstValidationContext attr_ctx = *ctx;
        attr_ctx.current_path = attr_path;
        
        // Validate attributes using map validation
        ValidationResult* attr_result = validate_against_map_type(validator, attr_item, map_part, &attr_ctx);
        
        // Merge attribute validation results
        if (attr_result && !attr_result->valid) {
            result->valid = false;
            result->error_count += attr_result->error_count;
            
            ValidationError* attr_error = attr_result->errors;
            while (attr_error) {
                ValidationError* copied_error = create_validation_error(
                    attr_error->code, attr_error->message->chars, 
                    attr_error->path, ctx->pool);
                add_validation_error(result, copied_error);
                attr_error = attr_error->next;
            }
        }
    }
    
    // Validate element content length
    if (element_type->content_length > 0) {
        // Create path segment for content
        PathSegment* content_path = nullptr;
        if (ctx->pool) {
            content_path = (PathSegment*)pool_calloc(ctx->pool, sizeof(PathSegment));
            if (content_path) {
                content_path->type = PATH_ELEMENT;
                content_path->data.element_tag = (StrView){"content", 7};
                content_path->next = ctx->current_path;
            }
        }
        
        if (element_data->length > element_type->content_length) {
            char content_error[256];
            snprintf(content_error, sizeof(content_error), 
                    "Element content length mismatch: expected %ld, got %ld", 
                    element_type->content_length, element_data->length);
            add_validation_error(result, create_validation_error(
                AST_VALID_ERROR_CONSTRAINT_VIOLATION, content_error, content_path, ctx->pool));
        }
    }
    
    return result;
}

ValidationResult* validate_against_union_type(AstValidator* validator, TypedItem item, Type** union_types, int type_count, AstValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);
    
    if (!union_types || type_count <= 0) {
        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Invalid union type definition", ctx->current_path, ctx->pool);
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
        if (ctx->pool) {
            union_path = (PathSegment*)pool_calloc(ctx->pool, sizeof(PathSegment));
            if (union_path) {
                union_path->type = PATH_INDEX;
                union_path->data.index = i;
                union_path->next = ctx->current_path;
            }
        }
        AstValidationContext union_ctx = *ctx;
        union_ctx.current_path = union_path;
        
        // Try validating against this union member
        ValidationResult* member_result = validate_against_type(validator, item, union_types[i], &union_ctx);
        
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
            ValidationError* error = best_result->errors;
            while (error) {
                ValidationError* copied_error = create_validation_error(
                    error->code, error->message->chars, error->path, ctx->pool);
                add_validation_error(result, copied_error);
                error = error->next;
            }
        } else {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Item does not match any type in union (%d types)", type_count);
            ValidationError* error = create_validation_error(
                AST_VALID_ERROR_TYPE_MISMATCH, error_msg, ctx->current_path, ctx->pool);
            add_validation_error(result, error);
        }
    }
    
    return result;
}

ValidationResult* validate_against_occurrence(AstValidator* validator, TypedItem* items, long item_count, Type* expected_type, Operator occurrence_op, AstValidationContext* ctx) {
    ValidationResult* result = create_validation_result(ctx->pool);
    
    if (!expected_type) {
        add_validation_error(result, create_validation_error(
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
                add_validation_error(result, create_validation_error(
                    AST_VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, ctx->current_path, ctx->pool));
            }
            break;
            
        case OPERATOR_ONE_MORE: // + (1 or more)
            if (item_count < 1) {
                add_validation_error(result, create_validation_error(
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
            add_validation_error(result, create_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, error_msg, ctx->current_path, ctx->pool));
            return result;
    }
    
    // Validate each item against the expected type
    for (long i = 0; i < item_count; i++) {
        // Create path segment for item index
        PathSegment* item_path = nullptr;
        if (ctx->pool) {
            item_path = (PathSegment*)pool_calloc(ctx->pool, sizeof(PathSegment));
            if (item_path) {
                item_path->type = PATH_INDEX;
                item_path->data.index = i;
                item_path->next = ctx->current_path;
            }
        }
        AstValidationContext item_ctx = *ctx;
        item_ctx.current_path = item_path;
        item_ctx.current_depth++;
        
        ValidationResult* item_result = validate_against_type(validator, items[i], expected_type, &item_ctx);
        
        if (item_result && !item_result->valid) {
            result->valid = false;
            result->error_count += item_result->error_count;
            
            ValidationError* item_error = item_result->errors;
            while (item_error) {
                ValidationError* copied_error = create_validation_error(
                    item_error->code, item_error->message->chars, 
                    item_error->path, ctx->pool);
                add_validation_error(result, copied_error);
                item_error = item_error->next;
            }
        }
    }
    
    return result;
}
