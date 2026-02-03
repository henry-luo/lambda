/**
 * @file validator_internal.hpp
 * @brief Internal helpers for Lambda Validator
 * 
 * This header provides internal utilities for validation:
 * - PathScope: RAII class for managing validation path
 * - DepthScope: RAII class for tracking validation depth
 * - Error helper functions
 * - Type unwrapping utilities
 */

#pragma once

#include "validator.hpp"

// ==================== RAII Scope Guards ====================

/**
 * PathScope - RAII guard for validation path management
 * 
 * Automatically pushes a path segment on construction and
 * restores the previous path on destruction.
 * 
 * Usage:
 *   {
 *       PathScope scope(validator, PATH_INDEX, index);
 *       // ... validation code ...
 *   } // path automatically restored
 */
class PathScope {
private:
    SchemaValidator* validator;
    PathSegment* prev_path;

public:
    // Constructor for index-based paths (arrays)
    PathScope(SchemaValidator* v, long index)
        : validator(v), prev_path(v->get_current_path()) {
        PathSegment* path = nullptr;
        Pool* pool = v->get_pool();
        if (pool) {
            path = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
            if (path) {
                path->type = PATH_INDEX;
                path->data.index = index;
                path->next = prev_path;
            }
        }
        v->set_current_path(path ? path : prev_path);
    }

    // Constructor for field-based paths (maps)
    PathScope(SchemaValidator* v, StrView field_name)
        : validator(v), prev_path(v->get_current_path()) {
        PathSegment* path = nullptr;
        Pool* pool = v->get_pool();
        if (pool) {
            path = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
            if (path) {
                path->type = PATH_FIELD;
                path->data.field_name = field_name;
                path->next = prev_path;
            }
        }
        v->set_current_path(path ? path : prev_path);
    }

    // Constructor for element paths
    PathScope(SchemaValidator* v, PathSegmentType type, StrView tag)
        : validator(v), prev_path(v->get_current_path()) {
        PathSegment* path = nullptr;
        Pool* pool = v->get_pool();
        if (pool) {
            path = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
            if (path) {
                path->type = type;
                if (type == PATH_ELEMENT) {
                    path->data.element_tag = tag;
                } else if (type == PATH_ATTRIBUTE) {
                    path->data.attr_name = tag;
                } else {
                    path->data.field_name = tag;
                }
                path->next = prev_path;
            }
        }
        v->set_current_path(path ? path : prev_path);
    }

    // Constructor for union paths
    PathScope(SchemaValidator* v, PathSegmentType type, long index)
        : validator(v), prev_path(v->get_current_path()) {
        PathSegment* path = nullptr;
        Pool* pool = v->get_pool();
        if (pool) {
            path = (PathSegment*)pool_calloc(pool, sizeof(PathSegment));
            if (path) {
                path->type = type;
                path->data.index = index;
                path->next = prev_path;
            }
        }
        v->set_current_path(path ? path : prev_path);
    }

    ~PathScope() {
        validator->set_current_path(prev_path);
    }

    // Non-copyable
    PathScope(const PathScope&) = delete;
    PathScope& operator=(const PathScope&) = delete;
};

/**
 * DepthScope - RAII guard for validation depth tracking
 * 
 * Automatically increments depth on construction and
 * decrements on destruction.
 */
class DepthScope {
private:
    SchemaValidator* validator;

public:
    explicit DepthScope(SchemaValidator* v) : validator(v) {
        v->set_current_depth(v->get_current_depth() + 1);
    }

    ~DepthScope() {
        validator->set_current_depth(validator->get_current_depth() - 1);
    }

    // Non-copyable
    DepthScope(const DepthScope&) = delete;
    DepthScope& operator=(const DepthScope&) = delete;
};

// ==================== Type Unwrapping Utilities ====================

/**
 * Unwrap nested TypeType wrappers to get the underlying type
 * 
 * @param type The type to unwrap
 * @return The innermost non-TypeType type, or nullptr if input is nullptr
 */
inline Type* unwrap_type(Type* type) {
    while (type && type->type_id == LMD_TYPE_TYPE) {
        type = ((TypeType*)type)->type;
    }
    return type;
}

/**
 * Check if a type is optional (TypeUnary with OPERATOR_OPTIONAL)
 * 
 * @param type The type to check (may be wrapped in TypeType)
 * @return true if the type is optional
 */
inline bool is_type_optional(Type* type) {
    Type* unwrapped = unwrap_type(type);
    if (unwrapped && (unwrapped->type_id == LMD_TYPE_TYPE_UNARY || 
                      unwrapped->type_id == LMD_TYPE_TYPE)) {
        TypeUnary* unary = (TypeUnary*)unwrapped;
        return unary->op == OPERATOR_OPTIONAL;
    }
    return false;
}

/**
 * Check if a type is a TypeUnary (occurrence operator)
 */
inline bool is_type_unary(Type* type) {
    Type* unwrapped = unwrap_type(type);
    return unwrapped && unwrapped->type_id == LMD_TYPE_TYPE_UNARY;
}

// ==================== Validation State Helpers ====================

/**
 * Check if validation should stop due to timeout
 */
bool should_stop_for_timeout(SchemaValidator* validator);

/**
 * Check if validation should stop due to max errors reached
 */
bool should_stop_for_max_errors(ValidationResult* result, int max_errors);

/**
 * Initialize validation session (for timeout tracking)
 */
void init_validation_session(SchemaValidator* validator);

// ==================== Error Helper Functions ====================

/**
 * Add a type mismatch error to the result
 */
void add_type_mismatch_error(
    ValidationResult* result,
    SchemaValidator* validator,
    const char* expected_type_name,
    TypeId actual_type_id
);

/**
 * Add a type mismatch error with Type* for expected
 */
void add_type_mismatch_error_ex(
    ValidationResult* result,
    SchemaValidator* validator,
    Type* expected_type,
    ConstItem actual_item
);

/**
 * Add a constraint violation error to the result
 */
void add_constraint_error(
    ValidationResult* result,
    SchemaValidator* validator,
    const char* message
);

/**
 * Add a constraint error with formatted message
 */
void add_constraint_error_fmt(
    ValidationResult* result,
    SchemaValidator* validator,
    const char* fmt,
    ...
);

/**
 * Add a missing field error
 */
void add_missing_field_error(
    ValidationResult* result,
    SchemaValidator* validator,
    const char* field_name
);

/**
 * Add a null value error
 */
void add_null_value_error(
    ValidationResult* result,
    SchemaValidator* validator,
    const char* field_name
);

/**
 * Merge errors from source result into destination
 */
void merge_errors(
    ValidationResult* dest,
    ValidationResult* src,
    SchemaValidator* validator
);

// ==================== Occurrence Count Helpers ====================

/**
 * Count constraint structure for occurrence validation
 */
struct CountConstraint {
    int min;    // minimum count (0 or greater)
    int max;    // maximum count (-1 means unbounded)
};

/**
 * Get count constraint from TypeUnary
 */
CountConstraint get_count_constraint(TypeUnary* type_unary);

/**
 * Check count against constraint and add error if violated
 * @return true if count is valid, false otherwise
 */
bool check_count_constraint(
    int count,
    CountConstraint constraint,
    ValidationResult* result,
    SchemaValidator* validator,
    const char* container_type  // "list", "array", etc.
);

// ==================== Pattern Validation (Forward Declarations) ====================

/**
 * Validate against TypeUnary (occurrence patterns: ?, +, *, [n], [n+], [n,m])
 */
ValidationResult* validate_occurrence_type(
    SchemaValidator* validator,
    ConstItem item,
    TypeUnary* type_unary
);

/**
 * Validate against TypeBinary (union |, intersection &, exclude \)
 */
ValidationResult* validate_binary_type(
    SchemaValidator* validator,
    ConstItem item,
    TypeBinary* type_binary
);

/**
 * Validate against union type (T1 | T2 | ...)
 * Note: Function name kept as validate_against_union_type for backward compatibility
 */
ValidationResult* validate_against_union_type(
    SchemaValidator* validator,
    ConstItem item,
    Type** union_types,
    int type_count
);

/**
 * Validate against base type (unwraps TypeType and dispatches)
 */
ValidationResult* validate_against_base_type(
    SchemaValidator* validator,
    ConstItem item,
    TypeType* type
);

// End of validator_internal.hpp
