/**
 * @file validate_pattern.cpp
 * @brief Pattern validation for Lambda Validator
 * 
 * Handles validation of:
 * - Occurrence patterns: ?, +, *, [n], [n+], [n,m]
 * - Union types: T1 | T2 | ...
 */

#include "validator_internal.hpp"
#include "../mark_reader.hpp"

// ==================== Forward Declarations ====================

// From validate.cpp
ValidationResult* validate_against_type(SchemaValidator* validator, ConstItem item, Type* type);

// ==================== Occurrence Validation ====================

/**
 * Validate a single item against TypeUnary when it's not a container
 * (handles the case where a single value matches occurrence of 1)
 */
static ValidationResult* validate_single_item_occurrence(
    SchemaValidator* validator,
    ConstItem item,
    TypeUnary* type_unary
) {
    ValidationResult* result = create_validation_result(validator->get_pool());
    CountConstraint constraint = get_count_constraint(type_unary);
    
    // For optional (?), null is valid
    if (type_unary->op == OPERATOR_OPTIONAL && item.type_id() == LMD_TYPE_NULL) {
        result->valid = true;
        return result;
    }
    
    // Single item can match occurrence of 1
    if (constraint.min <= 1 && (constraint.max == -1 || constraint.max >= 1)) {
        // Validate the single item against the operand type
        Type* operand_type = unwrap_type(type_unary->operand);
        if (operand_type) {
            TypeType temp_wrapper;
            temp_wrapper.type_id = LMD_TYPE_TYPE;
            temp_wrapper.type = operand_type;
            return validate_against_base_type(validator, item, &temp_wrapper);
        }
    }
    
    result->valid = false;
    add_type_mismatch_error(result, validator, "array/list", item.type_id());
    return result;
}

/**
 * Validate ArrayInt against occurrence type
 * ArrayInt stores raw int64 values, not tagged Items
 */
static ValidationResult* validate_array_int_occurrence(
    SchemaValidator* validator,
    const ArrayInt* arr_int,
    TypeUnary* type_unary
) {
    ValidationResult* result = create_validation_result(validator->get_pool());
    
    int count = arr_int ? (int)arr_int->length : 0;
    CountConstraint constraint = get_count_constraint(type_unary);
    
    log_debug("[PATTERN] ArrayInt occurrence: count=%d, min=%d, max=%d",
              count, constraint.min, constraint.max);
    
    // Check count constraints
    if (!check_count_constraint(count, constraint, result, validator, "Array")) {
        return result;
    }
    
    // For ArrayInt, the operand type must be compatible with int
    Type* operand_type = unwrap_type(type_unary->operand);
    
    if (operand_type && operand_type->type_id == LMD_TYPE_INT) {
        result->valid = true;
    } else {
        result->valid = false;
        add_constraint_error_fmt(result, validator,
            "ArrayInt elements are integers, but expected type_id=%d",
            operand_type ? operand_type->type_id : -1);
    }
    
    return result;
}

/**
 * Validate List/Array against occurrence type
 */
static ValidationResult* validate_list_occurrence(
    SchemaValidator* validator,
    const List* list,
    TypeUnary* type_unary
) {
    ValidationResult* result = create_validation_result(validator->get_pool());
    
    int count = list ? (int)list->length : 0;
    CountConstraint constraint = get_count_constraint(type_unary);
    
    log_debug("[PATTERN] List occurrence: count=%d, min=%d, max=%d",
              count, constraint.min, constraint.max);
    
    // Check count constraints
    if (!check_count_constraint(count, constraint, result, validator, "List")) {
        return result;
    }
    
    // Get operand type for element validation
    Type* operand_type = unwrap_type(type_unary->operand);
    
    if (!operand_type) {
        log_error("[PATTERN] TypeUnary operand is null after unwrapping");
        result->valid = false;
        return result;
    }
    
    // Validate each list element
    if (list && count > 0) {
        TypeType temp_wrapper;
        temp_wrapper.type_id = LMD_TYPE_TYPE;
        temp_wrapper.type = operand_type;
        
        for (int i = 0; i < count; i++) {
            PathScope scope(validator, (long)i);
            
            ConstItem elem = list->get(i);
            ValidationResult* elem_result = validate_against_base_type(validator, elem, &temp_wrapper);
            
            if (elem_result && !elem_result->valid) {
                merge_errors(result, elem_result, validator);
                return result;
            }
        }
    }
    
    result->valid = true;
    return result;
}

ValidationResult* validate_occurrence_type(
    SchemaValidator* validator,
    ConstItem item,
    TypeUnary* type_unary
) {
    log_debug("[PATTERN] validate_occurrence_type: op=%d, min=%d, max=%d",
              type_unary->op, type_unary->min_count, type_unary->max_count);
    
    TypeId item_type_id = item.type_id();
    
    // Check if item is a list/array
    bool is_container = (item_type_id == LMD_TYPE_LIST ||
                         item_type_id == LMD_TYPE_ARRAY ||
                         item_type_id == LMD_TYPE_ARRAY_INT ||
                         item_type_id == LMD_TYPE_ARRAY_INT64 ||
                         item_type_id == LMD_TYPE_ARRAY_FLOAT);
    
    if (!is_container) {
        return validate_single_item_occurrence(validator, item, type_unary);
    }
    
    // Handle typed arrays specially
    if (item_type_id == LMD_TYPE_ARRAY_INT) {
        return validate_array_int_occurrence(validator, item.array_int, type_unary);
    }
    
    // Handle generic List/Array
    return validate_list_occurrence(validator, item.list, type_unary);
}

// ==================== Union Type Validation ====================

ValidationResult* validate_against_union_type(
    SchemaValidator* validator,
    ConstItem item,
    Type** union_types,
    int type_count
) {
    ValidationResult* result = create_validation_result(validator->get_pool());

    if (!union_types || type_count <= 0) {
        add_constraint_error(result, validator, "Invalid union type definition");
        return result;
    }

    // Track the best result for error reporting
    ValidationResult* best_result = nullptr;
    int min_errors = INT32_MAX;
    int best_union_index = -1;

    log_debug("[PATTERN] Validating against union type with %d members", type_count);

    for (int i = 0; i < type_count; i++) {
        if (!union_types[i]) continue;

        log_debug("[PATTERN] Trying union member %d (type_id=%d)", i, union_types[i]->type_id);

        // Create scoped path for union member
        PathScope scope(validator, PATH_UNION, (long)i);

        // Try validating against this union member
        ValidationResult* member_result = validate_against_type(validator, item, union_types[i]);

        if (member_result && member_result->valid) {
            log_debug("[PATTERN] Union member %d matched successfully", i);
            result->valid = true;
            return result;
        }
        
        if (member_result) {
            log_debug("[PATTERN] Union member %d failed with %d errors", i, member_result->error_count);

            // Track the result with the fewest errors (most specific/helpful)
            if (member_result->error_count < min_errors) {
                min_errors = member_result->error_count;
                best_result = member_result;
                best_union_index = i;
            }
        }
    }

    // No type in the union was valid - report the best error
    result->valid = false;

    log_debug("[PATTERN] No union member matched. Best result from member %d with %d errors",
              best_union_index, min_errors);

    if (best_result && best_result->error_count > 0) {
        // Copy errors from the best result
        merge_errors(result, best_result, validator);

        // Add a summary error at the top level
        add_constraint_error_fmt(result, validator,
            "Item does not match any type in union (%d types tried, closest match was type #%d with %d error%s)",
            type_count, best_union_index, min_errors, min_errors == 1 ? "" : "s");
    } else {
        // No useful error information from union members
        add_constraint_error_fmt(result, validator,
            "Item does not match any type in union (%d types)", type_count);
    }

    return result;
}

// ==================== Legacy Occurrence Validation ====================

/**
 * Legacy function for validating occurrence constraints on item arrays
 * Used by external callers that pass explicit item arrays
 */
ValidationResult* validate_against_occurrence(
    SchemaValidator* validator,
    ConstItem* items,
    long item_count,
    Type* expected_type,
    Operator occurrence_op
) {
    ValidationResult* result = create_validation_result(validator->get_pool());

    if (!expected_type) {
        add_constraint_error(result, validator, "Invalid occurrence constraint parameters");
        return result;
    }

    // Validate occurrence constraints based on operator
    switch (occurrence_op) {
        case OPERATOR_OPTIONAL: // ? (0 or 1)
            if (item_count > 1) {
                add_constraint_error_fmt(result, validator,
                    "Optional constraint violated: expected 0 or 1 items, got %ld", item_count);
            }
            break;

        case OPERATOR_ONE_MORE: // + (1 or more)
            if (item_count < 1) {
                add_constraint_error(result, validator,
                    "One-or-more constraint violated: expected at least 1 item, got 0");
            }
            break;

        case OPERATOR_ZERO_MORE: // * (0 or more)
            // Always valid for zero-or-more
            break;

        default:
            add_constraint_error_fmt(result, validator,
                "Unsupported occurrence operator: %d", occurrence_op);
            return result;
    }

    // Validate each item against the expected type
    for (long i = 0; i < item_count; i++) {
        PathScope path_scope(validator, i);
        DepthScope depth_scope(validator);

        ValidationResult* item_result = validate_against_type(validator, items[i], expected_type);
        
        if (item_result && !item_result->valid) {
            merge_errors(result, item_result, validator);
        }
    }

    return result;
}
