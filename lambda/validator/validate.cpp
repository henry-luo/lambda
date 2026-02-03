#include "validator_internal.hpp"
#include "../mark_reader.hpp"  // MarkReader API for type-safe traversal

// Note: Helper functions (should_stop_for_timeout, should_stop_for_max_errors, 
// init_validation_session) are now in validate_helpers.cpp

ValidationResult* validate_against_primitive_type(SchemaValidator* validator, ConstItem item, Type* type) {
    log_debug("[VALIDATOR] Validating primitive: expected=%d, actual=%d", type->type_id, item.type_id());
    ValidationResult* result = create_validation_result(validator->get_pool());
    
    if (type->type_id == item.type_id()) {
        result->valid = true;
    } else {
        result->valid = false;
        add_type_mismatch_error_ex(result, validator, type, item);
    }
    return result;
}

ValidationResult* validate_against_base_type(SchemaValidator* validator, ConstItem item, TypeType* type) {
    ValidationResult* result = create_validation_result(validator->get_pool());
    Type* base_type = type->type;

    // Safety check for null base_type
    if (!base_type) {
        log_error("[VALIDATOR] Base type is null in TypeType wrapper");
        result->valid = false;
        return result;
    }

    log_debug("[VALIDATOR] validate_against_base_type: base_type->type_id=%d, item type_id=%d",
              base_type->type_id, item.type_id());

    // Unwrap nested TypeType wrappers
    base_type = unwrap_type(base_type);
    
    if (!base_type) {
        log_error("[VALIDATOR] Base type is null after unwrapping");
        result->valid = false;
        return result;
    }

    // Handle TypeUnary (occurrence operators: ?, +, *, [n], [n+], [n,m])
    if (base_type->type_id == LMD_TYPE_TYPE_UNARY) {
        return validate_occurrence_type(validator, item, (TypeUnary*)base_type);
    }

    // Handle TypeBinary (union/intersection: |, &, \)
    if (base_type->type_id == LMD_TYPE_TYPE_BINARY) {
        return validate_binary_type(validator, item, (TypeBinary*)base_type);
    }

    // Handle numeric types with promotion
    if (LMD_TYPE_INT <= base_type->type_id && base_type->type_id <= LMD_TYPE_NUMBER) {
        // Number promotion - allow int/float/decimal interchangeably
        if (LMD_TYPE_INT <= item.type_id() && item.type_id() <= base_type->type_id) {
            result->valid = true;
        } else {
            result->valid = false;
            add_type_mismatch_error_ex(result, validator, base_type, item);
        }
        return result;
    }
    
    // Handle compound types
    // Note: Must check for generic types (TYPE_MAP, TYPE_ELMT, TYPE_ARRAY) which are
    // simple Type structs, not TypeMap/TypeElmt/TypeArray. Casting them would read garbage.
    extern Type TYPE_MAP;
    extern Type TYPE_ELMT;
    extern TypeArray TYPE_ARRAY;
    
    if (base_type->type_id == LMD_TYPE_MAP) {
        if (base_type == &TYPE_MAP) {
            // Generic map type - just check if item is a map
            if (item.type_id() == LMD_TYPE_MAP) {
                result->valid = true;
            } else {
                add_type_mismatch_error(result, validator, "map", item.type_id());
            }
            return result;
        }
        return validate_against_map_type(validator, item, (TypeMap*)base_type);
    }
    if (base_type->type_id == LMD_TYPE_ELEMENT) {
        if (base_type == &TYPE_ELMT) {
            // Generic element type - just check if item is an element
            if (item.type_id() == LMD_TYPE_ELEMENT) {
                result->valid = true;
            } else {
                add_type_mismatch_error(result, validator, "element", item.type_id());
            }
            return result;
        }
        return validate_against_element_type(validator, item, (TypeElmt*)base_type);
    }
    if (base_type->type_id == LMD_TYPE_ARRAY || base_type->type_id == LMD_TYPE_LIST) {
        if (base_type == (Type*)&TYPE_ARRAY) {
            // Generic array type - just check if item is an array/list
            if (item.type_id() == LMD_TYPE_ARRAY || item.type_id() == LMD_TYPE_LIST ||
                item.type_id() == LMD_TYPE_ARRAY_INT || item.type_id() == LMD_TYPE_ARRAY_INT64 ||
                item.type_id() == LMD_TYPE_ARRAY_FLOAT) {
                result->valid = true;
            } else {
                add_type_mismatch_error(result, validator, "array", item.type_id());
            }
            return result;
        }
        return validate_against_array_type(validator, item, (TypeArray*)base_type);
    }
    
    // Direct type match
    if (base_type->type_id == item.type_id()) {
        result->valid = true;
    } else {
        result->valid = false;
        add_type_mismatch_error_ex(result, validator, base_type, item);
    }
    return result;
}

ValidationResult* validate_against_array_type(SchemaValidator* validator, ConstItem item, TypeArray* array_type) {
    log_debug("[VALIDATOR] Validating array type");
    ValidationResult* result = create_validation_result(validator->get_pool());

    // Use MarkReader for type-safe access
    ItemReader item_reader(item);

    // Check if item is actually an array/list
    if (!item_reader.isArray() && !item_reader.isList()) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
            "Type mismatch: expected array/list, got type %d", item_reader.getType());
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->get_current_path(), validator->get_pool()));
        return result;
    }

    // Get ArrayReader for type-safe iteration
    ArrayReader array = item_reader.asArray();
    int64_t length = array.length();

    log_debug("Validating array with length: %ld", length);

    // Check for occurrence operators on nested type
    // Nested type is TypeType* wrapping the actual type (which could be TypeUnary for occurrence operators)
    // We need to unwrap TypeType to check if it's a TypeUnary with occurrence operator
    log_debug("[AST_VALIDATOR] Checking array nested type at %p, type_id=%d",
              (void*)array_type->nested, array_type->nested ? array_type->nested->type_id : -1);

    if (array_type->nested && array_type->nested->type_id == LMD_TYPE_TYPE) {
        TypeType* type_wrapper = (TypeType*)array_type->nested;
        Type* unwrapped = type_wrapper->type;
        log_debug("[AST_VALIDATOR] Array nested is TypeType wrapper, unwrapped type at %p, type_id=%d",
                  (void*)unwrapped, unwrapped ? unwrapped->type_id : -1);

        // Check if unwrapped type is TypeUnary (occurrence operator)
        if (unwrapped && unwrapped->type_id == LMD_TYPE_TYPE) {
            TypeUnary* possible_unary = (TypeUnary*)unwrapped;
            if (possible_unary->op == OPERATOR_ONE_MORE && length < 1) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                        "Array with '+' occurrence operator requires at least one element, got %ld", length);
                add_validation_error(result, create_validation_error(
                    AST_VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, validator->get_current_path(), validator->get_pool()));
                return result;
            }
            else if (possible_unary->op == OPERATOR_OPTIONAL && length > 1) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                        "Array with '?' occurrence operator requires at most one element, got %ld", length);
                add_validation_error(result, create_validation_error(
                    AST_VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, validator->get_current_path(), validator->get_pool()));
                return result;
            }
            // OPERATOR_ZERO_MORE (*) has no length constraint
        }
    }

    // Validate array length if specified
    // if (array_type->length >= 0 && length != array_type->length) {
    //     char error_msg[256];
    //     snprintf(error_msg, sizeof(error_msg),
    //             "Array length mismatch: expected %ld, got %ld",
    //             array_type->length, length);
    //     add_validation_error(result, create_validation_error(
    //         AST_VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, validator->get_current_path(), validator->get_pool()));
    // }

    // Validate each array element against nested type using iterator
    if (array_type->nested && length > 0) {
        auto iter = array.items();
        ItemReader child;
        int64_t index = 0;

        while (iter.next(&child)) {
            PathScope scope(validator, index);

            // Recursively validate element
            log_debug("[VALIDATOR] Validating array item at index %ld", index);
            ConstItem child_item = child.item().to_const();
            ValidationResult* item_result = validate_against_type(
                validator, child_item, array_type->nested);

            // Merge validation results
            if (item_result && !item_result->valid) {
                merge_errors(result, item_result, validator);
            }

            index++;
        }
    }

    return result;
}

ValidationResult* validate_against_map_type(SchemaValidator* validator, ConstItem item, TypeMap* map_type) {
    ValidationResult* result = create_validation_result(validator->get_pool());

    // Check if item is actually a map using ItemReader
    ItemReader item_reader(item);
    if (!item_reader.isMap()) {
        add_type_mismatch_error(result, validator, "map", item.type_id());
        return result;
    }

    // If this is a generic map type (no shape defined), just check if item is a map
    if (!map_type->shape) {
        result->valid = true;
        return result;
    }

    // Use MapReader for type-safe access
    MapReader map = item_reader.asMap();
    const Map* raw_map = item.map;

    // Validate each field in the map shape
    ShapeEntry* shape_entry = map_type->shape;
    while (shape_entry) {
        if (!shape_entry->name) {
            log_error("[VALIDATOR] ShapeEntry has NULL name pointer");
            shape_entry = shape_entry->next;
            continue;
        }
        const char* field_name = shape_entry->name->str;

        // Use PathScope for automatic path management
        PathScope scope(validator, *shape_entry->name);

        // Check if field exists in the map's structure
        bool field_exists = raw_map && raw_map->has_field(field_name);

        if (!field_exists) {
            // Field is missing - check if it's optional
            if (!is_type_optional(shape_entry->type)) {
                add_missing_field_error(result, validator, field_name);
                result->valid = false;
            }
        } else {
            // Field exists - check if it's null
            ItemReader field_value = map.get(field_name);
            ConstItem field_item = field_value.item().to_const();

            if (field_item.type_id() == LMD_TYPE_NULL) {
                // Field is null - check if null is allowed
                if (!is_type_optional(shape_entry->type)) {
                    add_null_value_error(result, validator, field_name);
                    result->valid = false;
                }
            } else {
                // Field exists and is not null - validate its value
                log_debug("[VALIDATOR] Validating map field '%s'", field_name);
                ValidationResult* field_result = validate_against_type(validator, field_item, shape_entry->type);
                
                if (field_result && !field_result->valid) {
                    merge_errors(result, field_result, validator);
                }
            }
        }

        shape_entry = shape_entry->next;
    }

    return result;
}

ValidationResult* validate_against_element_type(SchemaValidator* validator, ConstItem item, TypeElmt* element_type) {
    ValidationResult* result = create_validation_result(validator->get_pool());

    // Check if item is actually an element
    ItemReader item_reader(item);
    if (!item_reader.isElement()) {
        add_type_mismatch_error(result, validator, "element", item.type_id());
        return result;
    }

    ElementReader element = item_reader.asElement();

    // Validate element name if specified
    if (element_type->name.length > 0) {
        const char* expected_tag = element_type->name.str;

        if (!element.hasTag(expected_tag)) {
            PathScope scope(validator, PATH_ELEMENT, element_type->name);
            add_constraint_error_fmt(result, validator,
                "Element tag mismatch: expected '%.*s', got '%s'",
                (int)element_type->name.length, expected_tag, element.tagName());
        }

        log_debug("[VALIDATOR] Validating element with tag '%.*s'",
                (int)element_type->name.length, expected_tag);
    }

    // TypeElmt inherits from TypeMap, so we can validate attributes as map fields
    TypeMap* map_part = (TypeMap*)element_type;
    if (map_part->shape) {
        PathScope attr_scope(validator, PATH_ATTRIBUTE, (StrView){"attrs", 5});

        // Validate each attribute
        ShapeEntry* shape_entry = map_part->shape;
        while (shape_entry) {
            if (!shape_entry->name) {
                log_error("[VALIDATOR] ShapeEntry has NULL name pointer");
                shape_entry = shape_entry->next;
                continue;
            }
            const char* attr_name = shape_entry->name->str;

            if (element.has_attr(attr_name)) {
                ItemReader attr_value = element.get_attr(attr_name);
                ConstItem attr_item = attr_value.item().to_const();

                log_debug("[VALIDATOR] Validating element attribute '%s'", attr_name);
                ValidationResult* attr_result = validate_against_type(validator, attr_item, shape_entry->type);

                if (attr_result && !attr_result->valid) {
                    merge_errors(result, attr_result, validator);
                }
            }

            shape_entry = shape_entry->next;
        }
    }

    // Validate element content length
    if (element_type->content_length > 0) {
        int64_t actual_length = element.childCount();

        if (actual_length != element_type->content_length) {
            PathScope scope(validator, PATH_ELEMENT, (StrView){"content", 7});
            add_constraint_error_fmt(result, validator,
                "Element content length mismatch: expected %lld, got %lld",
                element_type->content_length, actual_length);
        }
    }

    return result;
}

// ==================== Main Validation Dispatcher ====================

ValidationResult* validate_against_type(SchemaValidator* validator, ConstItem item, Type* type) {
    if (!validator || !type) {
        ValidationResult* result = create_validation_result(validator ? validator->get_pool() : nullptr);
        add_constraint_error(result, validator, "Invalid validation parameters");
        return result;
    }

    // Check for timeout
    if (should_stop_for_timeout(validator)) {
        ValidationResult* result = create_validation_result(validator->get_pool());
        add_constraint_error(result, validator, "Validation timeout exceeded");
        return result;
    }

    // Check validation depth
    if (validator->get_current_depth() >= validator->get_options()->max_depth) {
        ValidationResult* result = create_validation_result(validator->get_pool());
        add_constraint_error(result, validator, "Maximum validation depth exceeded");
        return result;
    }

    // Use RAII for depth tracking
    DepthScope depth_scope(validator);
    
    log_debug("[VALIDATOR] Validating type_id=%d against item type_id=%d", type->type_id, item.type_id());

    ValidationResult* result = nullptr;

    switch (type->type_id) {
        case LMD_TYPE_STRING:
        case LMD_TYPE_INT:
        case LMD_TYPE_FLOAT:
        case LMD_TYPE_BOOL:
        case LMD_TYPE_NULL:
            result = validate_against_primitive_type(validator, item, type);
            break;
            
        case LMD_TYPE_ARRAY:
        case LMD_TYPE_LIST:
            result = validate_against_array_type(validator, item, (TypeArray*)type);
            break;
            
        case LMD_TYPE_MAP: {
            extern Type TYPE_MAP;
            if (type == &TYPE_MAP) {
                // Generic map type - just check if item is a map
                result = create_validation_result(validator->get_pool());
                if (item.type_id() == LMD_TYPE_MAP) {
                    result->valid = true;
                } else {
                    add_type_mismatch_error(result, validator, "map", item.type_id());
                }
            } else {
                result = validate_against_map_type(validator, item, (TypeMap*)type);
            }
            break;
        }
        
        case LMD_TYPE_ELEMENT: {
            extern Type TYPE_ELMT;
            if (type == &TYPE_ELMT) {
                // Generic element type - just check if item is an element
                result = create_validation_result(validator->get_pool());
                if (item.type_id() == LMD_TYPE_ELEMENT) {
                    result->valid = true;
                } else {
                    add_type_mismatch_error(result, validator, "element", item.type_id());
                }
            } else {
                result = validate_against_element_type(validator, item, (TypeElmt*)type);
            }
            break;
        }
        
        case LMD_TYPE_TYPE:
            result = validate_against_base_type(validator, item, (TypeType*)type);
            break;
            
        case LMD_TYPE_TYPE_UNARY:
            // TypeUnary passed directly - delegate to occurrence validation
            result = validate_occurrence_type(validator, item, (TypeUnary*)type);
            break;
            
        case LMD_TYPE_TYPE_BINARY:
            // TypeBinary passed directly - delegate to binary type validation
            result = validate_binary_type(validator, item, (TypeBinary*)type);
            break;
            
        default:
            result = create_validation_result(validator->get_pool());
            add_constraint_error_fmt(result, validator, "Unsupported type for validation: %d", type->type_id);
            break;
    }

    // Check if we should stop due to max_errors
    if (should_stop_for_max_errors(result, validator->get_options()->max_errors)) {
        return result;
    }

    return result;
}
