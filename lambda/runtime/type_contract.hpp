// Shared semantic operations for function contracts and inferred type sets.
// These operate on compiler Type metadata only; they never alter Item tags.
#pragma once

#include "../lambda-data.hpp"

enum LambdaTypeExclusion {
    LAMBDA_TYPE_EXCLUDE_ERROR = 1u << 0,
    LAMBDA_TYPE_EXCLUDE_NULL = 1u << 1,
};

// Static half of an annotated boundary. PROVEN means the source type already
// satisfies the target, so the runtime check is redundant; DEFERRED means only
// the dynamic boundary can decide. `any` sources and unproven map shapes are
// deliberately DEFERRED rather than silently accepted.
enum StaticBoundaryResult {
    STATIC_BOUNDARY_PROVEN,
    STATIC_BOUNDARY_REJECTED,
    STATIC_BOUNDARY_DEFERRED,
};

// True when an annotated boundary from `source` to `target` needs no runtime
// check at all, so the MIR transpiler can skip emitting one.
//
// This is deliberately narrower than "the relation is PROVEN".
// `lambda_type_check` does not merely test a value — it returns whatever
// `runtime_type_admit_value` produced, which widens `int` to `float` and
// re-packs map shapes. A boundary can therefore be statically proven and still
// be load-bearing, so redundancy additionally requires that admission cannot
// change the representation.
bool lambda_boundary_is_redundant(Type* source, Type* target);

// C16: within the numeric tower, admission is decided by MEMBERSHIP at run time
// rather than by the static type -- `int` is the float64-representable
// integers, a subset of float and a superset of i32, so neither direction is
// statically refutable. Both the static relation (which DEFERS these pairs) and
// the MIR declaration boundary (which must therefore emit the runtime check)
// read this one predicate, so a pair cannot be deferred by one and skipped by
// the other.
bool boundary_numeric_admission_is_dynamic(TypeId source_id, TypeId target_id);

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
