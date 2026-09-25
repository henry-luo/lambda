/**
 * @file validate_helpers.cpp
 * @brief Helper functions for Lambda Validator
 * 
 * Contains:
 * - Validation state helpers (timeout, max errors)
 * - Error creation and merge helpers
 * - Count constraint validation
 */

#include "validator_internal.hpp"
#include <cstdarg>
#include <ctime>

// ==================== Validation State Helpers ====================

bool should_stop_for_timeout(SchemaValidator* validator) {
    if (validator->get_options()->timeout_ms <= 0) return false;
    if (validator->get_validation_start_time() == 0) return false;

    clock_t current = clock();
    double elapsed_ms = ((double)(current - validator->get_validation_start_time()) / CLOCKS_PER_SEC) * 1000.0;
    return elapsed_ms >= validator->get_options()->timeout_ms;
}

bool should_stop_for_max_errors(ValidationResult* result, int max_errors) {
    if (max_errors <= 0) return false;  // unlimited
    return result && result->error_count >= max_errors;
}

void init_validation_session(SchemaValidator* validator) {
    if (validator->get_options()->timeout_ms > 0) {
        validator->set_validation_start_time(clock());
    }
}

// ==================== Error Helper Functions ====================

void add_type_mismatch_error(
    ValidationResult* result,
    SchemaValidator* validator,
    const char* expected_type_name,
    TypeId actual_type_id
) {
    if (!result) return;
    
    char error_msg[256];
    const char* actual_type_name = (actual_type_id >= 0 && actual_type_id < 32)
        ? type_info[actual_type_id].name : "unknown";
    snprintf(error_msg, sizeof(error_msg),
            "Expected type '%s', but got '%s'",
            expected_type_name, actual_type_name);

    ValidationError* error = create_validation_error(
        AST_VALID_ERROR_TYPE_MISMATCH, error_msg,
        validator->get_current_path(), validator->get_pool());
    add_validation_error(result, error);
}

void add_type_mismatch_error_ex(
    ValidationResult* result,
    SchemaValidator* validator,
    Type* expected_type,
    ConstItem actual_item
) {
    if (!result) return;
    
    char error_msg[256];
    const char* actual_type_name = (actual_item.type_id() >= 0 && actual_item.type_id() < 32)
        ? type_info[actual_item.type_id()].name : "unknown";
    // a range contract is named by its bounds; its tag alone would read `type`
    char range_name[128];
    if (lambda_type_is_range(expected_type)) {
        lambda_type_format_contract_name(expected_type, range_name, sizeof(range_name));
    }
    snprintf(error_msg, sizeof(error_msg),
            "Expected type '%s', but got '%s'",
            lambda_type_is_range(expected_type) ? range_name : type_to_string(expected_type),
            actual_type_name);

    ValidationError* error = create_validation_error(
        AST_VALID_ERROR_TYPE_MISMATCH, error_msg,
        validator->get_current_path(), validator->get_pool());
    if (error) {
        error->expected = expected_type;
        error->actual = {.item = actual_item.item};
    }
    add_validation_error(result, error);
}

void add_constraint_error(
    ValidationResult* result,
    SchemaValidator* validator,
    const char* message
) {
    if (!result) return;
    
    ValidationError* error = create_validation_error(
        AST_VALID_ERROR_CONSTRAINT_VIOLATION, message,
        validator->get_current_path(), validator->get_pool());
    add_validation_error(result, error);
}

void add_constraint_error_fmt(
    ValidationResult* result,
    SchemaValidator* validator,
    const char* fmt,
    ...
) {
    if (!result) return;
    
    char error_msg[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(error_msg, sizeof(error_msg), fmt, args);
    va_end(args);

    ValidationError* error = create_validation_error(
        AST_VALID_ERROR_CONSTRAINT_VIOLATION, error_msg,
        validator->get_current_path(), validator->get_pool());
    add_validation_error(result, error);
}

static void add_field_error(ValidationResult* result, SchemaValidator* validator,
                            const char* field_name, ValidationErrorCode code,
                            const char* format) {
    if (!result) return;
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), format, field_name);
    ValidationError* error = create_validation_error(
        code, error_msg, validator->get_current_path(), validator->get_pool());
    add_validation_error(result, error);
}

void add_missing_field_error(
    ValidationResult* result,
    SchemaValidator* validator,
    const char* field_name
) {
    add_field_error(result, validator, field_name,
        AST_VALID_ERROR_MISSING_FIELD, "Required field '%s' is missing from object");
}

void add_null_value_error(
    ValidationResult* result,
    SchemaValidator* validator,
    const char* field_name
) {
    add_field_error(result, validator, field_name,
        AST_VALID_ERROR_NULL_VALUE, "Field cannot be null: %s");
}

void merge_errors(
    ValidationResult* dest,
    ValidationResult* src,
    SchemaValidator* validator
) {
    if (!dest || !src || src->valid) return;
    
    dest->valid = false;
    
    ValidationError* error = src->errors;
    while (error) {
        ValidationError* copied_error = create_validation_error(
            error->code, error->message ? error->message->chars : "Unknown error",
            error->path, validator->get_pool());
        if (copied_error) {
            copied_error->expected = error->expected;
            copied_error->actual = error->actual;
        }
        add_validation_error(dest, copied_error);
        error = error->next;
    }
}

// ==================== Count Constraint Helpers ====================

CountConstraint get_count_constraint(TypeUnary* type_unary) {
    CountConstraint c = {0, -1};  // default: 0 to unbounded
    if (!type_unary) return c;

    // `?`, `+` and `*` carry their bounds in the operator itself; only the
    // counted forms -- `T{n,m}` and the counted array `T[n]` -- read the
    // stored pair. Testing the pair for "was it set" cannot work now that
    // `int[0]` is a legitimate zero-length array (S11.1.6v2).
    switch (type_unary->op) {
        case OPERATOR_OPTIONAL:  c.min = 0; c.max = 1;  return c;   // ?
        case OPERATOR_ONE_MORE:  c.min = 1; c.max = -1; return c;   // +
        case OPERATOR_ZERO_MORE: c.min = 0; c.max = -1; return c;   // *
        default: break;                                 // {n,m} and [n]
    }
    c.min = type_unary->min_count;
    c.max = type_unary->max_count;
    return c;
}

// S11.1.6v2: as a boundary type an occurrence admits what a run *is*, so a
// container satisfies one only at two or more items -- one item is `[T]` and
// none is `[]`, both of the array family. The array family itself takes any
// length its own count allows.
CountConstraint get_container_count_constraint(TypeUnary* type_unary) {
    CountConstraint c = get_count_constraint(type_unary);
    if (type_unary && type_unary->op != OPERATOR_ARRAY && c.min < 2) c.min = 2;
    return c;
}

bool check_count_constraint(
    int count,
    CountConstraint constraint,
    ValidationResult* result,
    SchemaValidator* validator,
    const char* container_type
) {
    if (count < constraint.min) {
        add_constraint_error_fmt(result, validator,
            "%s has %d elements, but minimum required is %d",
            container_type, count, constraint.min);
        return false;
    }
    
    if (constraint.max != -1 && count > constraint.max) {
        add_constraint_error_fmt(result, validator,
            "%s has %d elements, but maximum allowed is %d",
            container_type, count, constraint.max);
        return false;
    }
    
    return true;
}
