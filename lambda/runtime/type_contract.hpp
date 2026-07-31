// Shared semantic operations for function contracts and inferred type sets.
// These operate on compiler Type metadata only; they never alter Item tags.
#pragma once

#include "../lambda-data.hpp"

enum LambdaTypeExclusion {
    LAMBDA_TYPE_EXCLUDE_ERROR = 1u << 0,
    LAMBDA_TYPE_EXCLUDE_NULL = 1u << 1,
};

bool lambda_type_accepts_error(Type* type);
bool lambda_type_accepts_null(Type* type);
bool lambda_type_has_proven_error(Type* type);
Type* lambda_type_union_normalized(Pool* pool, Type* left, Type* right);
Type* lambda_type_remove_exclusions(Pool* pool, Type* type, uint8_t exclusions);

// Render a semantic contract for source and runtime diagnostics. Extended
// Type values use LMD_TYPE_TYPE as an internal carrier, so callers must not
// derive a user-facing name from TypeId alone.
void lambda_type_format_name(const Type* type, char* buffer, size_t capacity);

// Re-represent an exactly admitted numeric value for a concrete boundary
// contract. This is deliberately separate from `lambda_type_matches()`, whose
// type-directional relation also drives `is` and pattern membership.
bool lambda_numeric_boundary_admit(Item value, Type* target, Item* converted);

static inline Type* lambda_type_remove_error(Pool* pool, Type* type) {
    return lambda_type_remove_exclusions(pool, type, LAMBDA_TYPE_EXCLUDE_ERROR);
}

static inline Type* lambda_type_remove_error_and_null(Pool* pool, Type* type) {
    return lambda_type_remove_exclusions(pool, type,
        LAMBDA_TYPE_EXCLUDE_ERROR | LAMBDA_TYPE_EXCLUDE_NULL);
}
