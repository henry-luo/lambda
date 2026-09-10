// Shared semantic operations for function contracts and inferred type sets.
// These operate on compiler Type metadata only; they never alter Item tags.
#pragma once

#include "../lambda-data.hpp"

enum LambdaTypeExclusion {
    LAMBDA_TYPE_EXCLUDE_ERROR = 1u << 0,
    LAMBDA_TYPE_EXCLUDE_NULL = 1u << 1,
};

// Compile-time result proof used by both boxed-return analysis and helper
// metadata. UNKNOWN is deliberately distinct from CAPABLE so open contracts
// fail closed instead of being treated as inline-only by a shallow TypeId.
enum LambdaWideResultProof {
    LAMBDA_WIDE_RESULT_UNKNOWN,
    LAMBDA_WIDE_RESULT_FREE,
    LAMBDA_WIDE_RESULT_CAPABLE,
};

LambdaWideResultProof lambda_type_wide_result_proof(const Type* type);
// This header is also included by guest AST shims inside an `extern "C"`
// block, where C++ overloads are illegal.
LambdaWideResultProof lambda_type_wide_result_proof_for_type_id(TypeId type_id);

// Static half of an annotated boundary. PROVEN means the source type already
// satisfies the target, so the runtime check is redundant; DEFERRED means only
// the dynamic boundary can decide. `any` sources and unproven map shapes are
// deliberately DEFERRED rather than silently accepted.
enum StaticBoundaryResult {
    STATIC_BOUNDARY_PROVEN,
    STATIC_BOUNDARY_REJECTED,
    STATIC_BOUNDARY_DEFERRED,
};

enum MapContractRelation {
    MAP_CONTRACT_INCOMPATIBLE,
    MAP_CONTRACT_EXACT_TRUSTED,
    MAP_CONTRACT_STORAGE_COMPATIBLE,
    MAP_CONTRACT_NEEDS_REIFICATION,
};

// One resolver for declared homogeneous arrays, inferred homogeneous arrays,
// and their nested ranks. TypeArray with per-slot patterns is a tuple and is
// deliberately excluded.
struct LambdaArrayContractInfo {
    Type* array_contract;
    Type* immediate_element;
    Type* leaf_element;
    LaneStorageDesc leaf_lane;
    uint8_t rank;
    uint8_t has_leaf_lane;
};

bool lambda_array_contract_info(Type* contract, LambdaArrayContractInfo* out);
// Resolve just the outer layer without deriving an unused leaf storage lane.
Type* lambda_array_contract_element(Type* contract);
// Return the resolver's canonical outer array node for an exact certificate
// comparison, or NULL when the contract is not a homogeneous value array.
Type* lambda_array_contract_canonical(Type* contract);
bool lambda_array_contract_compatible(Type* candidate, Type* expected,
    bool invariant);
// Return ArrayNum's exact scalar lane for a non-nullable, rank-one element
// contract. Pointer, nullable, abstract, and nested values have another
// carrier, so they deliberately return false.
bool lambda_array_num_elem_type_for_contract(Type* element,
    ArrayNumElemType* out_type);
ArrayRepCert* lambda_array_rep_cert_create(Pool* pool, Type* contract);
bool lambda_array_rep_proves(Item value, Type* target_contract, bool invariant);
bool lambda_array_rep_proves_cert(Item value, const ArrayRepCert* target,
    bool invariant);
void lambda_array_install_rep_cert(Item value, ArrayRepCert* cert);
void lambda_array_clear_rep_cert(Item value);

// resolve the canonical Lambda carrier from the complete semantic contract.
// this is a representation decision, not a physical MIR register query;
// abstract and heterogeneous contracts stay boxed (D2.4.1–D2.4.2).
ValueRep lambda_canonical_rep(Type* contract);
// apply the same canonical resolver to a compact dispatch key used by legacy
// compiler entry points; callers with a full contract must use the overload
// above (D2.4.1–D2.4.2).
ValueRep lambda_canonical_rep_for_type_id(TypeId type_id);

// Compare a runtime map shape with a concrete map contract. The exact result
// is reserved for the explicit compiler certificate; dynamic shapes never
// become trusted merely because their current fields happen to line up.
MapContractRelation lambda_map_contract_relation(const TypeMap* candidate,
        const TypeMap* expected);

// Compare one proven expression result with a map field contract without
// exposing the relation's recursive implementation to the MIR transpiler.
bool lambda_type_contract_semantically_compatible(Type* candidate, Type* expected);

// Resolve a concrete record layout through aliases and nullable spellings.
// A nullable receiver still needs its own value/null guard (D3.2.4v3).
Type* lambda_type_nonnull_map_contract(Type* contract);

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

// lambda_type_accepts_error / lambda_type_accepts_null live in the core
// header (lambda-data.hpp) beside the storage resolver they feed.
bool lambda_type_has_proven_error(Type* type);
// Native-lane projection of lambda_lane_storage_desc_for: false for contracts
// that must remain boxed (abstract, heterogeneous, non-nullable wide ints).
bool lambda_type_lane_storage_desc(Type* type, LaneStorageDesc* out);
// Canonicalize a semantic `T | null` result as `T?` when it has one concrete
// payload type. Abstract/error-bearing cases deliberately remain boxed.
Type* lambda_type_nullable_normalized(Pool* pool, Type* type);
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
