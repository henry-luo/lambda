#include "validator.hpp"
#include "../mark_reader.hpp"  // MarkReader API for type-safe traversal
#include <ctime>

// ==================== Validation State Helpers ====================

/**
 * Check if validation should stop due to timeout
 */
static bool should_stop_for_timeout(SchemaValidator* validator) {
    if (validator->get_options()->timeout_ms <= 0) return false;
    if (validator->get_validation_start_time() == 0) return false;

    clock_t current = clock();
    double elapsed_ms = ((double)(current - validator->get_validation_start_time()) / CLOCKS_PER_SEC) * 1000.0;
    return elapsed_ms >= validator->get_options()->timeout_ms;
}

/**
 * Check if validation should stop due to max errors reached
 */
static bool should_stop_for_max_errors(ValidationResult* result, int max_errors) {
    if (max_errors <= 0) return false;  // unlimited
    return result && result->error_count >= max_errors;
}

/**
 * Initialize validation session (for timeout tracking)
 */
static void init_validation_session(SchemaValidator* validator) {
    if (validator->get_options()->timeout_ms > 0) {
        validator->set_validation_start_time(clock());
    }
}

ValidationResult* validate_against_primitive_type(SchemaValidator* validator, ConstItem item, Type* type) {
    log_debug("[AST_VALIDATOR] Validating primitive: expected=%d, actual=%d", type->type_id, item.type_id());
    ValidationResult* result = create_validation_result(validator->get_pool());
    // todo: match literal values
    if (type->type_id == item.type_id()) {
        result->valid = true;
    } else {
        result->valid = false;
        char error_msg[256];
        const char* actual_type_name = item.type_id() >= 0 && item.type_id() < 32
            ? type_info[item.type_id()].name : "unknown";
        snprintf(error_msg, sizeof(error_msg),
                "Expected type '%s', but got '%s'",
                type_to_string(type), actual_type_name);

        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->get_current_path(), validator->get_pool());
        if (error) {
            error->expected = type;
            error->actual = {.item = item.item};
            add_validation_error(result, error);
        }
    }
    return result;
}

ValidationResult* validate_against_base_type(SchemaValidator* validator, ConstItem item, TypeType* type) {
    ValidationResult* result = create_validation_result(validator->get_pool());
    Type *base_type = type->type;

    // Safety check for null base_type
    if (!base_type) {
        log_error("[AST_VALIDATOR] Base type is null in TypeType wrapper");
        result->valid = false;
        return result;
    }

    log_debug("[AST_VALIDATOR] validate_against_base_type: TypeType wrapper at %p, base_type at %p with type_id=%d, item type_id=%d",
              (void*)type, (void*)base_type, base_type->type_id, item.type_id());

    // TEMPORARY: Force print for debugging
    fprintf(stderr, "[DEBUG_TRACE] validate_against_base_type: base_type->type_id=%d, item.type_id()=%d\n",
            base_type->type_id, item.type_id());

    // Check if base_type is TypeUnary (occurrence operator: ?, +, *)
    // TypeUnary inherits from Type and has type_id = LMD_TYPE_TYPE
    // We need to carefully check if this is actually a TypeUnary, not just any LMD_TYPE_TYPE
    // Use a while loop to unwrap nested TypeType wrappers
    while (base_type->type_id == LMD_TYPE_TYPE) {
        // Try casting to TypeUnary to check the operator
        TypeUnary* possible_unary = (TypeUnary*)base_type;

        log_debug("[AST_VALIDATOR] base_type type_id is LMD_TYPE_TYPE, checking if TypeUnary. op=%d", possible_unary->op);
        fprintf(stderr, "[DEBUG_TRACE] base_type is LMD_TYPE_TYPE, checking op field: op=%d\n", possible_unary->op);

        // Check if this is an occurrence operator
        // These are the only valid unary operators on types
        if (possible_unary->op == OPERATOR_OPTIONAL ||
            possible_unary->op == OPERATOR_ONE_MORE ||
            possible_unary->op == OPERATOR_ZERO_MORE) {

            fprintf(stderr, "[DEBUG_TRACE] IS TypeUnary with occurrence operator!\n");

            log_debug("[AST_VALIDATOR] Detected TypeUnary with occurrence operator: %d, operand at %p",
                      possible_unary->op, (void*)possible_unary->operand);

            // TEMPORARY: Force print for debugging
            fprintf(stderr, "[DEBUG_TRACE] TypeUnary detected: op=%d, operand=%p\n",
                    possible_unary->op, (void*)possible_unary->operand);

            // Validate against the operand type
            // Unwrap TypeType from operand if needed
            Type* operand_type = possible_unary->operand;
            log_debug("[AST_VALIDATOR] Before unwrap: operand_type at %p, type_id=%d",
                      (void*)operand_type, operand_type ? operand_type->type_id : -1);

            fprintf(stderr, "[DEBUG_TRACE] Before unwrap: operand_type=%p, type_id=%d\n",
                    (void*)operand_type, operand_type ? operand_type->type_id : -1);

            if (operand_type && operand_type->type_id == LMD_TYPE_TYPE) {
                TypeType* operand_wrapper = (TypeType*)operand_type;
                operand_type = operand_wrapper->type;
                log_debug("[AST_VALIDATOR] After unwrap: operand_type at %p, type_id=%d",
                          (void*)operand_type, operand_type ? operand_type->type_id : -1);
                fprintf(stderr, "[DEBUG_TRACE] After unwrap: operand_type=%p, type_id=%d\n",
                        (void*)operand_type, operand_type ? operand_type->type_id : -1);
            }

            if (!operand_type) {
                log_error("[AST_VALIDATOR] TypeUnary operand is null after unwrapping");
                result->valid = false;
                return result;
            }

            // Now wrap the operand in TypeType for validation (unless it's already wrapped)
            // If operand was already TypeType*, we unwrapped it, so rewrap
            // If operand was a primitive, wrap it
            TypeType temp_wrapper;
            temp_wrapper.type_id = LMD_TYPE_TYPE;
            temp_wrapper.type = operand_type;

            log_debug("[AST_VALIDATOR] Recursing into validate_against_base_type with wrapped operand");
            return validate_against_base_type(validator, item, &temp_wrapper);
        }

        // Not a TypeUnary with occurrence operator, must be TypeType wrapping something else
        // This happens with literal types like &LIT_TYPE_INT
        fprintf(stderr, "[DEBUG_TRACE] NOT a TypeUnary (op=%d), treating as TypeType wrapper\n", possible_unary->op);
        TypeType* nested_wrapper = (TypeType*)base_type;
        if (!nested_wrapper->type) {
            log_error("[AST_VALIDATOR] TypeType wrapper has null nested type");
            result->valid = false;
            return result;
        }

        fprintf(stderr, "[DEBUG_TRACE] Unwrapping TypeType: nested type_id=%d\n", nested_wrapper->type->type_id);
        // Update base_type to the wrapped type and loop to unwrap further if needed
        base_type = nested_wrapper->type;
        fprintf(stderr, "[DEBUG_TRACE] After unwrap iteration, base_type->type_id=%d\n", base_type->type_id);
        // Continue loop to check if this is also a TypeType that needs unwrapping
    }

    if (LMD_TYPE_INT <= base_type->type_id && base_type->type_id <= LMD_TYPE_NUMBER) {
        // number promotion - allow int/float/decimal interchangeably
        if (LMD_TYPE_INT <= item.type_id() && item.type_id() <= base_type->type_id) {
            result->valid = true;
        }
        else { result->valid = false; }
    }
    else if (base_type->type_id == LMD_TYPE_MAP) {
        // Base type is a map - validate structure recursively
        return validate_against_map_type(validator, item, (TypeMap*)base_type);
    }
    else if (base_type->type_id == LMD_TYPE_ELEMENT) {
        // Base type is an element - validate structure recursively
        return validate_against_element_type(validator, item, (TypeElmt*)base_type);
    }
    else if (base_type->type_id == LMD_TYPE_ARRAY || base_type->type_id == LMD_TYPE_LIST) {
        // Base type is an array/list - validate structure recursively
        return validate_against_array_type(validator, item, (TypeArray*)base_type);
    }
    else if (base_type->type_id == item.type_id()) {
        result->valid = true;
    } else {
        result->valid = false;
        char error_msg[256];
        const char* actual_type_name = item.type_id() >= 0 && item.type_id() < 32
            ? type_info[item.type_id()].name : "unknown";
        snprintf(error_msg, sizeof(error_msg),
                "Expected type '%s', but got '%s'",
                type_to_string(base_type), actual_type_name);

        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->get_current_path(), validator->get_pool());
        if (error) {
            error->expected = base_type;
            error->actual = (Item){.item = item.item};
            add_validation_error(result, error);
        }
    }
    return result;
}

ValidationResult* validate_against_array_type(SchemaValidator* validator, ConstItem item, TypeArray* array_type) {
    log_debug("Validating array type");
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
    // Pass nested type directly - validate_against_type will handle TypeType unwrapping
    if (array_type->nested && length > 0) {

        auto iter = array.items();
        ItemReader child;
        int64_t index = 0;

        while (iter.next(&child)) {
            // Create path segment for array index
            PathSegment* path = nullptr;
            if (validator->get_pool()) {
                path = (PathSegment*)pool_calloc(validator->get_pool(), sizeof(PathSegment));
                if (path) {
                    path->type = PATH_INDEX;
                    path->data.index = index;
                    path->next = validator->get_current_path();
                }
            }
            PathSegment* prev_path = validator->get_current_path();
            validator->set_current_path(path);

            // Recursively validate element
            log_debug("[AST_VALIDATOR] Validating array item at index %ld, item type_id=%d, against nested type at %p type_id=%d",
                      index, child.getType(), (void*)array_type->nested,
                      array_type->nested ? array_type->nested->type_id : -1);
            ConstItem child_item = child.item().to_const();
            ValidationResult* item_result = validate_against_type(
                validator, child_item, array_type->nested);

            // Restore previous path
            validator->set_current_path(prev_path);

            // Merge validation results
            if (item_result && !item_result->valid) {
                result->valid = false;
                // Add all element errors to main result
                ValidationError* element_error = item_result->errors;
                while (element_error) {
                    result->error_count++;
                    // Copy error to main result
                    ValidationError* copied_error = create_validation_error(
                        element_error->code, element_error->message->chars,
                        element_error->path, validator->get_pool());
                    add_validation_error(result, copied_error);
                    element_error = element_error->next;
                }
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
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "Type mismatch: expected map, got type %d", item.type_id());
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->get_current_path(), validator->get_pool()));
        return result;
    }

    // If this is a generic map type (no shape defined), just check if item is a map
    // Generic types like TYPE_MAP are just Type structs, not full TypeMap structs
    // So accessing map_type->shape would be undefined behavior
    if (!map_type->shape) {
        // Item is a map and type is generic map - validation passes
        result->valid = true;
        return result;
    }

    // Use MapReader for type-safe access
    MapReader map = item_reader.asMap();
    const Map* raw_map = item.map;

    // Validate each field in the map shape
    ShapeEntry* shape_entry = map_type->shape;
    while (shape_entry) {
        // Check if shape_entry->name is valid before dereferencing
        if (!shape_entry->name) {
            log_error("ShapeEntry has NULL name pointer in map validation");
            shape_entry = shape_entry->next;
            continue;
        }
        const char* field_name = shape_entry->name->str;

        // Create path segment for map field
        PathSegment* field_path = nullptr;
        if (validator->get_pool()) {
            field_path = (PathSegment*)pool_calloc(validator->get_pool(), sizeof(PathSegment));
            if (field_path) {
                field_path->type = PATH_FIELD;
                field_path->data.field_name = *shape_entry->name;
                field_path->next = validator->get_current_path();
            }
        }
        PathSegment* saved_path = validator->get_current_path();
        validator->set_current_path(field_path);

        // Check if field exists in the map's structure (not just has non-null value)
        bool field_exists = raw_map && raw_map->has_field(field_name);

        if (!field_exists) {
            // Field is truly missing - check if it's optional
            // A field is optional if its type is wrapped in OPERATOR_OPTIONAL (Type?)
            // Need to unwrap TypeType to check if it's TypeUnary with OPERATOR_OPTIONAL
            bool is_optional = false;
            Type* field_type = shape_entry->type;
            if (field_type && field_type->type_id == LMD_TYPE_TYPE) {
                TypeType* type_wrapper = (TypeType*)field_type;
                Type* unwrapped = type_wrapper->type;
                if (unwrapped && unwrapped->type_id == LMD_TYPE_TYPE) {
                    TypeUnary* possible_unary = (TypeUnary*)unwrapped;
                    if (possible_unary->op == OPERATOR_OPTIONAL) {
                        is_optional = true;
                    }
                }
            }

            if (!is_optional) {
                // Required field is missing
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                        "Required field '%s' is missing from object", field_name);
                add_validation_error(result, create_validation_error(
                    AST_VALID_ERROR_MISSING_FIELD, error_msg,
                    validator->get_current_path(), validator->get_pool()));
                result->valid = false;
            }
            // If field is optional and missing, that's OK - skip validation
        } else {
            // Field exists - check if it's null
            ItemReader field_value = map.get(field_name);
            ConstItem field_item = field_value.item().to_const();

            if (field_item.type_id() == LMD_TYPE_NULL) {
                // Field exists but is null - check if null is allowed
                // Need to unwrap TypeType to check if it's TypeUnary with OPERATOR_OPTIONAL
                bool allows_null = false;
                Type* field_type = shape_entry->type;
                if (field_type && field_type->type_id == LMD_TYPE_TYPE) {
                    TypeType* type_wrapper = (TypeType*)field_type;
                    Type* unwrapped = type_wrapper->type;
                    if (unwrapped && unwrapped->type_id == LMD_TYPE_TYPE) {
                        TypeUnary* possible_unary = (TypeUnary*)unwrapped;
                        if (possible_unary->op == OPERATOR_OPTIONAL) {
                            allows_null = true;
                        }
                    }
                }

                if (!allows_null) {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg),
                            "Field cannot be null: %s", field_name);
                    add_validation_error(result, create_validation_error(
                        AST_VALID_ERROR_NULL_VALUE, error_msg,
                        validator->get_current_path(), validator->get_pool()));
                    result->valid = false;
                }
                // If null is allowed, that's OK - skip further validation
            } else {
                // Field exists and is not null - validate its value
                log_debug("Validating map field '%s', type %d", field_name, field_value.getType());
                ValidationResult* field_result = validate_against_type(validator, field_item, shape_entry->type);

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
                            field_error->path, validator->get_pool());
                        add_validation_error(result, copied_error);

                        field_error = field_error->next;
                    }
                }
            }
        }

        validator->set_current_path(saved_path);
        shape_entry = shape_entry->next;
    }

    return result;
}

ValidationResult* validate_against_element_type(SchemaValidator* validator, ConstItem item, TypeElmt* element_type) {
    ValidationResult* result = create_validation_result(validator->get_pool());

    // Check if item is actually an element using ItemReader
    ItemReader item_reader(item);
    if (!item_reader.isElement()) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "Type mismatch: expected element, got type %d", item.type_id());
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->get_current_path(), validator->get_pool()));
        return result;
    }

    // Use ElementReader for type-safe access
    ElementReader element = item_reader.asElement();

    // Validate element name if specified
    PathSegment* saved_path = validator->get_current_path();
    if (element_type->name.length > 0) {
        const char* expected_tag = element_type->name.str;

        // Check if element has the expected tag
        if (!element.hasTag(expected_tag)) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg),
                    "Element tag mismatch: expected '%.*s', got '%s'",
                    (int)element_type->name.length, expected_tag, element.tagName());

            PathSegment* element_name_path = nullptr;
            if (validator->get_pool()) {
                element_name_path = (PathSegment*)pool_calloc(validator->get_pool(), sizeof(PathSegment));
                if (element_name_path) {
                    element_name_path->type = PATH_ELEMENT;
                    element_name_path->data.element_tag = element_type->name;
                    element_name_path->next = validator->get_current_path();
                }
            }

            add_validation_error(result, create_validation_error(
                AST_VALID_ERROR_TYPE_MISMATCH, error_msg, element_name_path, validator->get_pool()));
        }

        log_debug("Validating element with tag '%.*s'",
                (int)element_type->name.length, expected_tag);
    }

    // TypeElmt inherits from TypeMap, so we can validate attributes as map fields
    TypeMap* map_part = (TypeMap*)element_type;
    if (map_part->shape) {
        // Create path segment for attributes
        PathSegment* attr_path = nullptr;
        if (validator->get_pool()) {
            attr_path = (PathSegment*)pool_calloc(validator->get_pool(), sizeof(PathSegment));
            if (attr_path) {
                attr_path->type = PATH_ATTRIBUTE;
                attr_path->data.attr_name = (StrView){"attrs", 5};
                attr_path->next = validator->get_current_path();
            }
        }
        validator->set_current_path(attr_path);

        // Validate each attribute using ElementReader
        ShapeEntry* shape_entry = map_part->shape;
        while (shape_entry) {
            // Check if shape_entry->name is valid before dereferencing
            if (!shape_entry->name) {
                log_error("ShapeEntry has NULL name pointer in element validation");
                shape_entry = shape_entry->next;
                continue;
            }
            const char* attr_name = shape_entry->name->str;

            if (element.has_attr(attr_name)) {
                ItemReader attr_value = element.get_attr(attr_name);
                ConstItem attr_item = attr_value.item().to_const();

                log_debug("Validating element attribute '%s', type %d", attr_name, attr_value.getType());
                ValidationResult* attr_result = validate_against_type(validator, attr_item, shape_entry->type);

                // Merge attribute validation results
                if (attr_result && !attr_result->valid) {
                    result->valid = false;

                    ValidationError* attr_error = attr_result->errors;
                    while (attr_error) {
                        result->error_count++;

                        ValidationError* copied_error = create_validation_error(
                            attr_error->code, attr_error->message->chars,
                            attr_error->path, validator->get_pool());
                        add_validation_error(result, copied_error);
                        attr_error = attr_error->next;
                    }
                }
            }
            // Note: Missing attribute validation would go here if required attributes are tracked

            shape_entry = shape_entry->next;
        }

        validator->set_current_path(saved_path);
    }

    // Validate element content length
    if (element_type->content_length > 0) {
        int64_t actual_length = element.childCount();

        if (actual_length != element_type->content_length) {
            PathSegment* content_path = nullptr;
            if (validator->get_pool()) {
                content_path = (PathSegment*)pool_calloc(validator->get_pool(), sizeof(PathSegment));
                if (content_path) {
                    content_path->type = PATH_ELEMENT;
                    content_path->data.element_tag = (StrView){"content", 7};
                    content_path->next = validator->get_current_path();
                }
            }

            char content_error[256];
            snprintf(content_error, sizeof(content_error),
                    "Element content length mismatch: expected %lld, got %lld",
                    element_type->content_length, actual_length);
            add_validation_error(result, create_validation_error(
                AST_VALID_ERROR_CONSTRAINT_VIOLATION, content_error, content_path, validator->get_pool()));
        }
    }

    validator->set_current_path(saved_path);
    return result;
}

ValidationResult* validate_against_union_type(SchemaValidator* validator, ConstItem item, Type** union_types, int type_count) {
    ValidationResult* result = create_validation_result(validator->get_pool());

    if (!union_types || type_count <= 0) {
        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Invalid union type definition", validator->get_current_path(), validator->get_pool());
        add_validation_error(result, error);
        return result;
    }

    // Try to validate against each type in the union
    bool any_valid = false;
    ValidationResult* best_result = nullptr;
    int min_errors = 2147483647; // INT_MAX value
    int best_union_index = -1;

    log_debug("[AST_VALIDATOR] Validating against union type with %d members", type_count);

    for (int i = 0; i < type_count; i++) {
        if (!union_types[i]) continue;

        log_debug("[AST_VALIDATOR] Trying union member %d (type_id=%d)", i, union_types[i]->type_id);

        // Create path segment for union member
        PathSegment* union_path = nullptr;
        if (validator->get_pool()) {
            union_path = (PathSegment*)pool_calloc(validator->get_pool(), sizeof(PathSegment));
            if (union_path) {
                union_path->type = PATH_UNION;
                union_path->data.index = i;
                union_path->next = validator->get_current_path();
            }
        }
        PathSegment* prev_path = validator->get_current_path();
        validator->set_current_path(union_path);

        // Try validating against this union member
        ValidationResult* member_result = validate_against_type(validator, item, union_types[i]);

        if (member_result && member_result->valid) {
            log_debug("[AST_VALIDATOR] Union member %d matched successfully", i);
            any_valid = true;
            result->valid = true;
            validator->set_current_path(prev_path);
            return result;
        } else if (member_result) {
            log_debug("[AST_VALIDATOR] Union member %d failed with %d errors", i, member_result->error_count);

            // Track the result with the fewest errors (most specific/helpful)
            if (member_result->error_count < min_errors) {
                min_errors = member_result->error_count;
                best_result = member_result;
                best_union_index = i;
            }
        }

        validator->set_current_path(prev_path);
    }

    // If no type in the union was valid, report the best error
    if (!any_valid) {
        result->valid = false;

        log_debug("[AST_VALIDATOR] No union member matched. Best result from member %d with %d errors",
                  best_union_index, min_errors);

        if (best_result && best_result->error_count > 0) {
            // Copy errors from the best result
            result->error_count = best_result->error_count;
            ValidationError* error = best_result->errors;
            while (error) {
                ValidationError* copied_error = create_validation_error(
                    error->code, error->message->chars, error->path, validator->get_pool());
                if (copied_error) {
                    copied_error->expected = error->expected;
                    copied_error->actual = error->actual;
                }
                add_validation_error(result, copied_error);
                error = error->next;
            }

            // Add a summary error at the top level indicating union failure
            char summary_msg[512];
            snprintf(summary_msg, sizeof(summary_msg),
                    "Item does not match any type in union (%d types tried, closest match was type #%d with %d error%s)",
                    type_count, best_union_index, min_errors, min_errors == 1 ? "" : "s");
            ValidationError* summary_error = create_validation_error(
                AST_VALID_ERROR_TYPE_MISMATCH, summary_msg, validator->get_current_path(), validator->get_pool());
            add_validation_error(result, summary_error);
        } else {
            // No useful error information from union members
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg),
                    "Item does not match any type in union (%d types)", type_count);
            ValidationError* error = create_validation_error(
                AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->get_current_path(), validator->get_pool());
            add_validation_error(result, error);
        }
    }

    return result;
}

ValidationResult* validate_against_occurrence(SchemaValidator* validator, ConstItem* items, long item_count, Type* expected_type, Operator occurrence_op) {
    ValidationResult* result = create_validation_result(validator->get_pool());

    if (!expected_type) {
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_PARSE_ERROR, "Invalid occurrence constraint parameters", validator->get_current_path(), validator->get_pool()));
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
                    AST_VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, validator->get_current_path(), validator->get_pool()));
            }
            break;

        case OPERATOR_ONE_MORE: // + (1 or more)
            if (item_count < 1) {
                add_validation_error(result, create_validation_error(
                    AST_VALID_ERROR_CONSTRAINT_VIOLATION,
                    "One-or-more constraint violated: expected at least 1 item, got 0",
                    validator->get_current_path(), validator->get_pool()));
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
                AST_VALID_ERROR_PARSE_ERROR, error_msg, validator->get_current_path(), validator->get_pool()));
            return result;
    }

    // Validate each item against the expected type
    for (long i = 0; i < item_count; i++) {
        // Create path segment for item index
        PathSegment* item_path = nullptr;
        if (validator->get_pool()) {
            item_path = (PathSegment*)pool_calloc(validator->get_pool(), sizeof(PathSegment));
            if (item_path) {
                item_path->type = PATH_INDEX;
                item_path->data.index = i;
                item_path->next = validator->get_current_path();
            }
        }
        PathSegment* pa_path = validator->get_current_path();
        validator->set_current_path(item_path);
        validator->set_current_depth(validator->get_current_depth() + 1);

        ValidationResult* item_result = validate_against_type(validator, items[i], expected_type);
        if (item_result && !item_result->valid) {
            result->valid = false;
            result->error_count += item_result->error_count;

            ValidationError* item_error = item_result->errors;
            while (item_error) {
                ValidationError* copied_error = create_validation_error(
                    item_error->code, item_error->message->chars,
                    item_error->path, validator->get_pool());
                add_validation_error(result, copied_error);
                item_error = item_error->next;
            }
        }
        validator->set_current_path(pa_path);  validator->set_current_depth(validator->get_current_depth() - 1);
    }

    return result;
}

/*
ValidationResult* validate_against_reference(SchemaValidator* validator, ConstItem typed_item, Type* schema, AstValidationContext* ctx) {
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

ValidationResult* validate_against_type(SchemaValidator* validator, ConstItem item, Type* type) {
    if (!validator || !type) {
        ValidationResult* result = create_validation_result(validator ? validator->get_pool() : nullptr);
        if (result) {
            ValidationError* error = create_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, "Invalid validation parameters",
                nullptr, validator ? validator->get_pool() : nullptr);
            add_validation_error(result, error);
        }
        return result;
    }

    // check for timeout
    if (should_stop_for_timeout(validator)) {
        ValidationResult* result = create_validation_result(validator->get_pool());
        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_CONSTRAINT_VIOLATION, "Validation timeout exceeded",
            validator->get_current_path(), validator->get_pool());
        add_validation_error(result, error);
        return result;
    }

    // check validation depth
    if (validator->get_current_depth() >= validator->get_options()->max_depth) {
        ValidationResult* result = create_validation_result(validator->get_pool());
        ValidationError* error = create_validation_error(
            AST_VALID_ERROR_CONSTRAINT_VIOLATION, "Maximum validation depth exceeded",
            validator->get_current_path(), validator->get_pool());
        add_validation_error(result, error);
        return result;
    }

    ValidationResult* result = nullptr;
    validator->set_current_depth(validator->get_current_depth() + 1);
    log_debug("[AST_VALIDATOR] Validating against type_id: %d, item type_id: %d", type->type_id, item.type_id());
    fprintf(stderr, "[VALIDATE_TYPE] type->type_id=%d, item.type_id()=%d\n", type->type_id, item.type_id());

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
        case LMD_TYPE_MAP: {
            // Check if this is a generic TYPE_MAP (just a Type struct) or a full TypeMap
            // Generic types are global constants and just check the type_id match
            TypeMap* map_type = (TypeMap*)type;
            // Check if we can safely access map_type fields
            // A real TypeMap allocated from pool will have shape == nullptr or valid pointer
            // But TYPE_MAP (global) will have garbage in those fields
            // Simple heuristic: if type == &TYPE_MAP (global), treat as generic
            extern Type TYPE_MAP;
            if (type == &TYPE_MAP) {
                // Generic map type - just check if item is a map
                result = create_validation_result(validator->get_pool());
                if (item.type_id() == LMD_TYPE_MAP) {
                    result->valid = true;
                } else {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg),
                            "Type mismatch: expected map, got type %d", item.type_id());
                    add_validation_error(result, create_validation_error(
                        AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->get_current_path(), validator->get_pool()));
                }
            } else {
                result = validate_against_map_type(validator, item, map_type);
            }
            // check max_errors after complex validation
            if (should_stop_for_max_errors(result, validator->get_options()->max_errors)) {
                validator->set_current_depth(validator->get_current_depth() - 1);
                return result;
            }
            validator->set_current_depth(validator->get_current_depth() - 1);
            return result;
        }
        case LMD_TYPE_ELEMENT: {
            // Same check for generic element type
            extern Type TYPE_ELMT;
            if (type == &TYPE_ELMT) {
                // Generic element type - just check if item is an element
                result = create_validation_result(validator->get_pool());
                if (item.type_id() == LMD_TYPE_ELEMENT) {
                    result->valid = true;
                } else {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg),
                            "Type mismatch: expected element, got type %d", item.type_id());
                    add_validation_error(result, create_validation_error(
                        AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->get_current_path(), validator->get_pool()));
                }
            } else {
                result = validate_against_element_type(validator, item, (TypeElmt*)type);
            }
            if (should_stop_for_max_errors(result, validator->get_options()->max_errors)) {
                validator->set_current_depth(validator->get_current_depth() - 1);
                return result;
            }
            validator->set_current_depth(validator->get_current_depth() - 1);
            return result;
        }
        case LMD_TYPE_TYPE:
            result = validate_against_base_type(validator, item, (TypeType*)type);
            validator->set_current_depth(validator->get_current_depth() - 1);
            return result;
        default:
            result = create_validation_result(validator->get_pool());
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg),
                    "Unsupported type for validation: %d", type->type_id);
            add_validation_error(result, create_validation_error(
                AST_VALID_ERROR_PARSE_ERROR, error_msg, validator->get_current_path(), validator->get_pool()));
            break;
    }

    // check if we should stop due to max_errors
    if (should_stop_for_max_errors(result, validator->get_options()->max_errors)) {
        validator->set_current_depth(validator->get_current_depth() - 1);
        return result;
    }

    validator->set_current_depth(validator->get_current_depth() - 1);
    return result;
}
