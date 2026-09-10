#include "type_contract.hpp"
#include "lambda-number-types.hpp"
#include "lambda-number-runtime.hpp"
#include "lambda-root-frame.hpp"
#include <mpdecimal.h>
#include <stdio.h>

// One spelling of the simple-TypeType unwrap: the core header's
// type_field_unwrap_simple_decl (the three global meta-types are compact Type
// values; it inspects `kind` only after excluding them).
static inline Type* contract_unwrap_type(Type* type) {
    return type_field_unwrap_simple_decl(type);
}

Type* lambda_type_nonnull_map_contract(Type* contract) {
    for (int depth = 0; contract && depth < 64; depth++) {
        contract = contract_unwrap_type(contract);
        if (!contract) return NULL;
        if (contract->type_id == LMD_TYPE_MAP) {
            return contract != &TYPE_MAP && contract != &TYPE_OBJECT ? contract : NULL;
        }
        if (contract->type_id != LMD_TYPE_TYPE) return NULL;
        if (contract->kind == TYPE_KIND_PARAM) {
            TypeParam* parameter = (TypeParam*)contract;
            contract = parameter->contract_type ? parameter->contract_type : parameter->full_type;
        } else if (contract->kind == TYPE_KIND_UNARY &&
                ((TypeUnary*)contract)->op == OPERATOR_OPTIONAL) {
            contract = ((TypeUnary*)contract)->operand;
        } else if (lambda_type_is_union(contract)) {
            TypeBinary* binary = (TypeBinary*)contract;
            Type* left = contract_unwrap_type(binary->left);
            Type* right = contract_unwrap_type(binary->right);
            if (left && left->type_id == LMD_TYPE_NULL) contract = right;
            else if (right && right->type_id == LMD_TYPE_NULL) contract = left;
            else return NULL;
        } else {
            // A layout alone cannot discharge a value-dependent refinement.
            return NULL;
        }
    }
    return NULL;
}

static Type* canonical_contract_base(Type* type, int depth) {
    if (!type || depth > 64) return NULL;
    type = contract_unwrap_type(type);
    if (!type) return NULL;
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_PARAM) {
        TypeParam* parameter = (TypeParam*)type;
        Type* full = parameter->contract_type ? parameter->contract_type :
            parameter->full_type;
        return full && full != type ? canonical_contract_base(full, depth + 1) : NULL;
    }
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_CONSTRAINED) {
        TypeConstrained* constrained = (TypeConstrained*)type;
        return canonical_contract_base(constrained->base, depth + 1);
    }
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_UNARY) {
        TypeUnary* unary = (TypeUnary*)type;
        if (unary->op == OPERATOR_OPTIONAL) {
            return canonical_contract_base(unary->operand, depth + 1);
        }
        // repeated values are containers, not the element's carrier.
        return NULL;
    }
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_BINARY) {
        // a union's runtime tag records the selected member. no single raw
        // register carrier is valid until admission has selected that member.
        return NULL;
    }
    return type;
}

ValueRep lambda_canonical_rep(Type* contract) {
    if (!contract) return VALUE_REP_ITEM;

    LaneStorageDesc lane = {};
    if (lambda_type_lane_storage_desc(contract, &lane)) {
        switch (lane.kind) {
        case LANE_STORAGE_INT: return VALUE_REP_INT_LANE;
        case LANE_STORAGE_FLOAT64: return VALUE_REP_F64;
        case LANE_STORAGE_BOOL: return VALUE_REP_I64;
        case LANE_STORAGE_SIZED_I64: return VALUE_REP_I64;
        case LANE_STORAGE_ITEM: return VALUE_REP_ITEM;
        case LANE_STORAGE_POINTER:
            return lane.base_contract && lane.base_contract->type_id == LMD_TYPE_TYPE
                ? VALUE_REP_RAW_NON_GC_POINTER : VALUE_REP_RAW_GC_POINTER;
        default: break;
        }
    }

    Type* base = canonical_contract_base(contract, 0);
    if (!base || base == &TYPE_ANY || base == &TYPE_INTEGER ||
            base == &TYPE_NUMBER) {
        return VALUE_REP_ITEM;
    }
    switch (base->type_id) {
    case LMD_TYPE_INT: return VALUE_REP_INT_LANE;
    case LMD_TYPE_INT64: return VALUE_REP_I64;
    case LMD_TYPE_UINT64: return VALUE_REP_U64;
    case LMD_TYPE_FLOAT: return VALUE_REP_F64;
    case LMD_TYPE_TYPE: return VALUE_REP_RAW_NON_GC_POINTER;
    case LMD_TYPE_DECIMAL:
    case LMD_TYPE_DTIME:
    case LMD_TYPE_STRING:
    case LMD_TYPE_SYMBOL:
    case LMD_TYPE_BINARY:
    case LMD_TYPE_COMPLEX:
    case LMD_TYPE_PATH:
    case LMD_TYPE_RANGE:
    case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_MAP:
    case LMD_TYPE_VMAP:
    case LMD_TYPE_VARRAY:
    case LMD_TYPE_VELMT:
    case LMD_TYPE_ELEMENT:
    case LMD_TYPE_FUNC:
        return VALUE_REP_RAW_GC_POINTER;
    case LMD_TYPE_BOOL:
        return VALUE_REP_I64;
    default:
        return VALUE_REP_ITEM;
    }
}

static Type* canonical_type_for_id(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_BOOL: return &TYPE_BOOL;
    case LMD_TYPE_INT: return &TYPE_INT;
    case LMD_TYPE_INT64: return &TYPE_INT64;
    case LMD_TYPE_FLOAT: return &TYPE_FLOAT;
    case LMD_TYPE_COMPLEX: return &TYPE_COMPLEX;
    case LMD_TYPE_DECIMAL: return &TYPE_DECIMAL;
    case LMD_TYPE_STRING: return &TYPE_STRING;
    case LMD_TYPE_BINARY: return &TYPE_BINARY;
    case LMD_TYPE_SYMBOL: return &TYPE_SYMBOL;
    case LMD_TYPE_PATH: return &TYPE_PATH;
    case LMD_TYPE_NUM_SIZED: return &TYPE_NUM_SIZED;
    case LMD_TYPE_UINT64: return &TYPE_UINT64;
    case LMD_TYPE_DTIME: return &TYPE_DTIME;
    case LMD_TYPE_ARRAY_NUM: return (Type*)&TYPE_ARRAY;
    case LMD_TYPE_ARRAY: return (Type*)&TYPE_ARRAY;
    case LMD_TYPE_VARRAY: return (Type*)&TYPE_ARRAY;
    case LMD_TYPE_RANGE: return &TYPE_RANGE;
    case LMD_TYPE_MAP: return &TYPE_MAP;
    case LMD_TYPE_VMAP: return &TYPE_MAP;
    case LMD_TYPE_VELMT: return &TYPE_ELMT;
    case LMD_TYPE_ELEMENT: return &TYPE_ELMT;
    case LMD_TYPE_TYPE: return &TYPE_TYPE;
    case LMD_TYPE_FUNC: return &TYPE_FUNC;
    case LMD_TYPE_ANY: return &TYPE_ANY;
    default: return NULL;
    }
}

ValueRep lambda_canonical_rep_for_type_id(TypeId type_id) {
    return lambda_canonical_rep(canonical_type_for_id(type_id));
}

static LambdaWideResultProof wide_result_join(LambdaWideResultProof left,
        LambdaWideResultProof right) {
    if (left == LAMBDA_WIDE_RESULT_CAPABLE ||
            right == LAMBDA_WIDE_RESULT_CAPABLE) {
        return LAMBDA_WIDE_RESULT_CAPABLE;
    }
    if (left == LAMBDA_WIDE_RESULT_UNKNOWN ||
            right == LAMBDA_WIDE_RESULT_UNKNOWN) {
        return LAMBDA_WIDE_RESULT_UNKNOWN;
    }
    return LAMBDA_WIDE_RESULT_FREE;
}

static LambdaWideResultProof wide_result_proof_inner(const Type* type,
        int depth) {
    if (!type || depth > 64) return LAMBDA_WIDE_RESULT_UNKNOWN;

    // `any`, its exclusion variants, and abstract integer/number contracts do
    // not name a closed value domain. Their physical carrier is not proof of
    // an inline result (D2.4.1).
    if (type->type_id == LMD_TYPE_ANY || type == &TYPE_INTEGER ||
            type == &TYPE_NUMBER || type == &TYPE_TYPE) {
        return LAMBDA_WIDE_RESULT_UNKNOWN;
    }

    if (type->type_id == LMD_TYPE_TYPE && !type_is_global_meta_type(type)) {
        switch (type->kind) {
        case TYPE_KIND_SIMPLE: {
            const TypeType* wrapper = (const TypeType*)type;
            return wrapper->type
                ? wide_result_proof_inner(wrapper->type, depth + 1)
                : LAMBDA_WIDE_RESULT_UNKNOWN;
        }
        case TYPE_KIND_PARAM: {
            const TypeParam* param = (const TypeParam*)type;
            const Type* contract = param->contract_type
                ? param->contract_type : param->full_type;
            return contract ? wide_result_proof_inner(contract, depth + 1)
                : LAMBDA_WIDE_RESULT_UNKNOWN;
        }
        case TYPE_KIND_CONSTRAINED: {
            const TypeConstrained* constrained = (const TypeConstrained*)type;
            return constrained->base
                ? wide_result_proof_inner(constrained->base, depth + 1)
                : LAMBDA_WIDE_RESULT_UNKNOWN;
        }
        case TYPE_KIND_UNARY: {
            const TypeUnary* unary = (const TypeUnary*)type;
            if (unary->op == OPERATOR_ARRAY) {
                // An array value is a container regardless of its element
                // contract; the result itself cannot be a wide scalar.
                return LAMBDA_WIDE_RESULT_FREE;
            }
            if (unary->op == OPERATOR_OPTIONAL) {
                return wide_result_join(LAMBDA_WIDE_RESULT_FREE,
                    wide_result_proof_inner(unary->operand, depth + 1));
            }
            return LAMBDA_WIDE_RESULT_UNKNOWN;
        }
        case TYPE_KIND_BINARY: {
            const TypeBinary* binary = (const TypeBinary*)type;
            if (binary->op != OPERATOR_UNION) {
                return LAMBDA_WIDE_RESULT_UNKNOWN;
            }
            return wide_result_join(
                wide_result_proof_inner(binary->left, depth + 1),
                wide_result_proof_inner(binary->right, depth + 1));
        }
        default:
            return LAMBDA_WIDE_RESULT_UNKNOWN;
        }
    }

    // D2.2.2's packed int and pointer-backed values are wide-free. Only the
    // out-of-band scalar family can carry a companion payload (D2.2.3).
    if (type->type_id == LMD_TYPE_FLOAT ||
            type->type_id == LMD_TYPE_INT64 ||
            type->type_id == LMD_TYPE_UINT64) {
        return LAMBDA_WIDE_RESULT_CAPABLE;
    }
    return LAMBDA_WIDE_RESULT_FREE;
}

LambdaWideResultProof lambda_type_wide_result_proof(const Type* type) {
    return wide_result_proof_inner(type, 0);
}

LambdaWideResultProof lambda_type_wide_result_proof_for_type_id(TypeId type_id) {
    if (type_id == LMD_TYPE_ANY || type_id == LMD_TYPE_TYPE)
        return LAMBDA_WIDE_RESULT_UNKNOWN;
    if (type_id == LMD_TYPE_FLOAT ||
            type_id == LMD_TYPE_INT64 || type_id == LMD_TYPE_UINT64) {
        return LAMBDA_WIDE_RESULT_CAPABLE;
    }
    return LAMBDA_WIDE_RESULT_FREE;
}

static bool contract_storage_desc_equal(const ShapeEntry* left,
        const ShapeEntry* right) {
    // one stored descriptor per field (SCU9): compare the records
    const LaneStorageDesc* l = shape_entry_storage(left);
    const LaneStorageDesc* r = shape_entry_storage(right);
    if (l->kind != r->kind || l->byte_size != r->byte_size ||
            l->nullable != r->nullable || l->native != r->native ||
            l->value_domain != r->value_domain) {
        return false;
    }
    return !lane_desc_is_nullable_native(l) || l->base_contract == r->base_contract;
}

static bool contract_semantics_equal(const Type* left, const Type* right) {
    left = contract_unwrap_type((Type*)left);
    right = contract_unwrap_type((Type*)right);
    if (left == right) return true;
    if (!left || !right || left->type_id != right->type_id || left->kind != right->kind) {
        return false;
    }
    if (left->type_id == LMD_TYPE_TYPE && left->kind == TYPE_KIND_UNARY) {
        const TypeUnary* left_unary = (const TypeUnary*)left;
        const TypeUnary* right_unary = (const TypeUnary*)right;
        return left_unary->op == right_unary->op &&
            contract_semantics_equal(left_unary->operand, right_unary->operand);
    }
    if (left->type_id == LMD_TYPE_TYPE && left->kind == TYPE_KIND_BINARY) {
        const TypeBinary* left_binary = (const TypeBinary*)left;
        const TypeBinary* right_binary = (const TypeBinary*)right;
        return left_binary->op == right_binary->op &&
            contract_semantics_equal(left_binary->left, right_binary->left) &&
            contract_semantics_equal(left_binary->right, right_binary->right);
    }
    return left->type_id != LMD_TYPE_MAP && left->type_id != LMD_TYPE_ARRAY &&
        left->type_id != LMD_TYPE_ELEMENT;
}

static bool contract_semantics_compatible(const Type* candidate,
        const Type* expected) {
    candidate = contract_unwrap_type((Type*)candidate);
    expected = contract_unwrap_type((Type*)expected);
    if (contract_semantics_equal(candidate, expected)) return true;
    if (!candidate || !expected) return false;

    if (expected->type_id == LMD_TYPE_TYPE && expected->kind == TYPE_KIND_UNARY) {
        const TypeUnary* expected_unary = (const TypeUnary*)expected;
        if (expected_unary->op == OPERATOR_OPTIONAL) {
            if (candidate->type_id == LMD_TYPE_NULL) return true;
            return contract_semantics_compatible(candidate, expected_unary->operand);
        }
        if (candidate->type_id != LMD_TYPE_TYPE ||
                candidate->kind != TYPE_KIND_UNARY) return false;
        const TypeUnary* candidate_unary = (const TypeUnary*)candidate;
        return candidate_unary->op == expected_unary->op &&
            contract_semantics_compatible(candidate_unary->operand,
                expected_unary->operand);
    }
    if (expected->type_id == LMD_TYPE_TYPE && expected->kind == TYPE_KIND_BINARY) {
        const TypeBinary* expected_binary = (const TypeBinary*)expected;
        if (expected_binary->op == OPERATOR_UNION) {
            return contract_semantics_compatible(candidate, expected_binary->left) ||
                contract_semantics_compatible(candidate, expected_binary->right);
        }
    }

    // Numeric boundary admission permits a narrower integer carrier at a
    // wider numeric field; the runtime conversion still decides exact value
    // membership before the destination lane is published.
    LambdaNumericKind candidate_kind = lambda_numeric_kind_from_type((Type*)candidate);
    LambdaNumericKind expected_kind = lambda_numeric_kind_from_type((Type*)expected);
    if (candidate_kind != LAMBDA_NUM_INVALID && expected_kind != LAMBDA_NUM_INVALID) {
        return true;
    }
    return false;
}

bool lambda_type_contract_semantically_compatible(Type* candidate, Type* expected) {
    return contract_semantics_compatible(candidate, expected);
}

static Type* array_contract_unwrap(Type* type, int depth) {
    if (!type || depth > 32) return NULL;
    type = contract_unwrap_type(type);
    if (!type) return NULL;
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_PARAM) {
        TypeParam* parameter = (TypeParam*)type;
        Type* contract = parameter->contract_type ? parameter->contract_type :
            parameter->full_type;
        return contract && contract != type
            ? array_contract_unwrap(contract, depth + 1) : NULL;
    }
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_CONSTRAINED) {
        Type* base = ((TypeConstrained*)type)->base;
        return base && base != type ? array_contract_unwrap(base, depth + 1) : NULL;
    }
    return type;
}

static bool array_contract_layer(Type* type, Type** element) {
    if (!type || !element) return false;
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_UNARY) {
        TypeUnary* unary = (TypeUnary*)type;
        if (unary->op == OPERATOR_ARRAY && unary->operand) {
            *element = unary->operand;
            return true;
        }
    }
    // TypeArray is the inferred homogeneous-array carrier. A tuple/pattern
    // retains its per-slot metadata and is never silently treated as T[].
    if ((type->type_id == LMD_TYPE_ARRAY || type->type_id == LMD_TYPE_ARRAY_NUM) &&
            type != &TYPE_ARRAY && type != &TYPE_LIST) {
        TypeArray* array = (TypeArray*)type;
        if (!array->item_patterns && array->nested) {
            *element = array->nested;
            return true;
        }
    }
    return false;
}

bool lambda_array_contract_info(Type* contract, LambdaArrayContractInfo* out) {
    if (!out) return false;
    *out = {};
    Type* root = array_contract_unwrap(contract, 0);
    if (!root) return false;

    Type* current = root;
    Type* element = NULL;
    while (out->rank < 32 && array_contract_layer(current, &element)) {
        element = array_contract_unwrap(element, 0);
        if (!element) return false;
        if (out->rank == 0) out->immediate_element = element;
        out->leaf_element = element;
        out->rank++;
        current = element;
    }
    if (out->rank == 0) return false;

    out->array_contract = root;
    out->leaf_lane = lambda_lane_storage_desc_for(out->leaf_element);
    out->has_leaf_lane = out->leaf_lane.kind != LANE_STORAGE_INVALID;
    return true;
}

Type* lambda_array_contract_element(Type* contract) {
    Type* element = NULL;
    return array_contract_layer(array_contract_unwrap(contract, 0), &element)
        ? array_contract_unwrap(element, 0) : NULL;
}

Type* lambda_array_contract_canonical(Type* contract) {
    Type* root = array_contract_unwrap(contract, 0);
    Type* element = NULL;
    return array_contract_layer(root, &element) ? root : NULL;
}

static bool array_contract_semantically_equal(Type* left, Type* right) {
    left = array_contract_unwrap(left, 0);
    right = array_contract_unwrap(right, 0);
    for (;;) {
        Type* left_element = NULL;
        Type* right_element = NULL;
        bool left_is_array = array_contract_layer(left, &left_element);
        bool right_is_array = array_contract_layer(right, &right_element);
        if (left_is_array != right_is_array) return false;
        if (!left_is_array) return contract_semantics_equal(left, right);
        left = array_contract_unwrap(left_element, 0);
        right = array_contract_unwrap(right_element, 0);
        if (!left || !right) return false;
    }
}

bool lambda_array_contract_compatible(Type* candidate, Type* expected,
        bool invariant) {
    if (invariant) {
        // Equality needs rank and leaf semantics, not two freshly derived
        // storage descriptions. Non-array identities are not array proofs.
        if (!lambda_array_contract_canonical(candidate) ||
                !lambda_array_contract_canonical(expected)) return false;
        return candidate == expected || array_contract_semantically_equal(candidate, expected);
    }
    LambdaArrayContractInfo candidate_info = {};
    LambdaArrayContractInfo expected_info = {};
    if (!lambda_array_contract_info(candidate, &candidate_info) ||
            !lambda_array_contract_info(expected, &expected_info) ||
            candidate_info.rank != expected_info.rank) {
        return false;
    }
    if (expected_info.immediate_element == &TYPE_ANY) return true;
    return contract_semantics_compatible(candidate_info.immediate_element,
        expected_info.immediate_element);
}

bool lambda_array_num_elem_type_for_contract(Type* element,
        ArrayNumElemType* out_type) {
    if (!element || !out_type || lambda_type_accepts_null(element)) return false;
    element = contract_unwrap_type(element);
    if (!element) return false;
    switch (element->type_id) {
    case LMD_TYPE_INT: *out_type = ELEM_INT; return true;
    case LMD_TYPE_INT64: *out_type = ELEM_INT64; return true;
    case LMD_TYPE_UINT64: *out_type = ELEM_UINT64; return true;
    case LMD_TYPE_FLOAT: *out_type = ELEM_FLOAT64; return true;
    case LMD_TYPE_BOOL: *out_type = ELEM_BOOL; return true;
    case LMD_TYPE_NUM_SIZED:
        switch (type_num_sized_kind(element)) {
        case NUM_INT8: *out_type = ELEM_INT8; return true;
        case NUM_INT16: *out_type = ELEM_INT16; return true;
        case NUM_INT32: *out_type = ELEM_INT32; return true;
        case NUM_UINT8: *out_type = ELEM_UINT8; return true;
        case NUM_UINT16: *out_type = ELEM_UINT16; return true;
        case NUM_UINT32: *out_type = ELEM_UINT32; return true;
        case NUM_FLOAT16: *out_type = ELEM_FLOAT16; return true;
        case NUM_FLOAT32: *out_type = ELEM_FLOAT32; return true;
        default: return false;
        }
    default:
        return false;
    }
}

ArrayRepCert* lambda_array_rep_cert_create(Pool* pool, Type* contract) {
    LambdaArrayContractInfo info = {};
    if (!pool || !lambda_array_contract_info(contract, &info)) return NULL;
    ArrayRepCert* cert = (ArrayRepCert*)pool_calloc(pool, sizeof(ArrayRepCert));
    if (!cert) return NULL;
    cert->array_contract = info.array_contract;
    cert->immediate_element = info.immediate_element;
    cert->leaf_element = info.leaf_element;
    cert->leaf_lane = info.leaf_lane;
    cert->rank = info.rank;
    cert->flags = ARRAY_REP_CERT_EXACT | ARRAY_REP_CERT_ERROR_FREE;
    ArrayNumElemType element = ELEM_INT;
    cert->has_array_num_lane = info.rank == 1 &&
        lambda_array_num_elem_type_for_contract(info.leaf_element, &element);
    cert->array_num_elem = element;
    if (info.leaf_element && (info.leaf_element->type_id == LMD_TYPE_MAP ||
            info.leaf_element->type_id == LMD_TYPE_ELEMENT)) {
        cert->flags |= ARRAY_REP_CERT_REIFIED;
    }
    return cert;
}

static ArrayRepCert* array_rep_cert_for_value(Item value) {
    TypeId type_id = get_type_id(value);
    if (type_id != LMD_TYPE_ARRAY && type_id != LMD_TYPE_ARRAY_NUM &&
            type_id != LMD_TYPE_ELEMENT) {
        return NULL;
    }
    return value.array ? value.array->rep_cert : NULL;
}

static bool array_cert_requires_native_lane(const ArrayRepCert* cert) {
    if (!cert || cert->rank != 1) return false;
    // Pointer values always need their raw T* lane. Nullable scalar lanes are
    // likewise represented by a native Array lane; non-nullable scalars use
    // ArrayNum and generic/union contracts remain boxed Items.
    return cert->leaf_lane.kind == LANE_STORAGE_POINTER ||
        (cert->leaf_lane.native && cert->leaf_lane.nullable);
}

static bool array_representation_matches_cert(Item value,
        const ArrayRepCert* cert) {
    if (!cert || cert->rank == 0) return false;
    TypeId value_type = get_type_id(value);
    if (value_type == LMD_TYPE_ARRAY_NUM) {
        // The immutable certificate already resolved nullable/sized numeric
        // spellings; only the live carrier can have changed (D3.3.3v3).
        return cert->has_array_num_lane && value.array_num &&
            value.array_num->get_elem_type() == cert->array_num_elem;
    }
    if (value_type != LMD_TYPE_ARRAY || !value.array) return false;
    if (cert->rank != 1) {
        // A nested T[][] outer carrier contains boxed child array Items.
        return !array_has_native_lane(value.array);
    }
    if (array_cert_requires_native_lane(cert)) {
        return array_native_lane_matches_desc(value.array, &cert->leaf_lane);
    }
    // A rank-one abstract/union/null contract owns ordinary boxed Items.
    return !array_has_native_lane(value.array);
}

bool lambda_array_rep_proves(Item value, Type* target_contract, bool invariant) {
    ArrayRepCert* cert = array_rep_cert_for_value(value);
    if (!cert || !(cert->flags & ARRAY_REP_CERT_EXACT) || !cert->array_contract) {
        return false;
    }
    return array_representation_matches_cert(value, cert) &&
        (cert->array_contract == target_contract ||
         lambda_array_contract_compatible(cert->array_contract, target_contract, invariant));
}

bool lambda_array_rep_proves_cert(Item value, const ArrayRepCert* target, bool invariant) {
    ArrayRepCert* cert = array_rep_cert_for_value(value);
    if (!cert || !target || !(cert->flags & ARRAY_REP_CERT_EXACT)) return false;
    return array_representation_matches_cert(value, cert) &&
        (cert == target || lambda_array_contract_compatible(cert->array_contract,
            target->array_contract, invariant));
}

void lambda_array_install_rep_cert(Item value, ArrayRepCert* cert) {
    TypeId type_id = get_type_id(value);
    if ((type_id == LMD_TYPE_ARRAY || type_id == LMD_TYPE_ARRAY_NUM ||
            type_id == LMD_TYPE_ELEMENT) && value.array) {
        value.array->rep_cert = cert;
    }
}

void lambda_array_clear_rep_cert(Item value) {
    lambda_array_install_rep_cert(value, NULL);
}

MapContractRelation lambda_map_contract_relation(const TypeMap* candidate,
        const TypeMap* expected) {
    if (!candidate || !expected) return MAP_CONTRACT_INCOMPATIBLE;
    if (candidate == expected && expected->is_trusted_contract) {
        return MAP_CONTRACT_EXACT_TRUSTED;
    }
    if (candidate->length != expected->length) return MAP_CONTRACT_INCOMPATIBLE;

    bool storage_compatible = true;
    for (ShapeEntry* expected_field = expected->shape; expected_field;
            expected_field = expected_field->next) {
        if (!expected_field->name || !expected_field->type) continue;
        ShapeEntry* candidate_field = typemap_hash_lookup((TypeMap*)candidate,
            expected_field->name->str, (int)expected_field->name->length);
        if (!candidate_field || !contract_semantics_compatible(candidate_field->type,
                expected_field->type)) {
            return MAP_CONTRACT_INCOMPATIBLE;
        }
        if (candidate_field->byte_offset != expected_field->byte_offset ||
                !contract_storage_desc_equal(candidate_field, expected_field)) {
            storage_compatible = false;
        }
    }
    return storage_compatible ? MAP_CONTRACT_STORAGE_COMPATIBLE
        : MAP_CONTRACT_NEEDS_REIFICATION;
}

static const Type* contract_display_unwrap_type(const Type* type) {
    Type* unwrapped = contract_unwrap_type((Type*)type);
    if (unwrapped && unwrapped->type_id == LMD_TYPE_TYPE &&
            unwrapped->kind == TYPE_KIND_CONSTRAINED) {
        Type* base = ((TypeConstrained*)unwrapped)->base;
        if (base) return contract_display_unwrap_type(base);
    }
    return unwrapped;
}

static void lambda_type_format_name_inner(const Type* type, char* buffer,
        size_t capacity, int depth) {
    if (!buffer || capacity == 0) return;
    type = contract_display_unwrap_type(type);
    if (!type) {
        snprintf(buffer, capacity, "unknown");
        return;
    }
    if (depth >= 8) {
        snprintf(buffer, capacity, "...");
        return;
    }
    if (type->type_id == LMD_TYPE_MAP && type != &TYPE_MAP) {
        // TYPE_MAP is a compact generic Type, not a TypeMap; only shaped maps own struct_name.
        const TypeMap* map_type = (const TypeMap*)type;
        if (map_type->struct_name) {
            snprintf(buffer, capacity, "%s", map_type->struct_name);
            return;
        }
    }
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_UNARY) {
        const TypeUnary* unary = (const TypeUnary*)type;
        char operand_name[128];
        lambda_type_format_name_inner(unary->operand, operand_name,
            sizeof(operand_name), depth + 1);
        if (unary->op == OPERATOR_ARRAY) {
            snprintf(buffer, capacity, "%s[]", operand_name);
            return;
        }
        if (unary->op == OPERATOR_REPEAT) {
            if (unary->max_count < 0) {
                snprintf(buffer, capacity, "%s[%d+]", operand_name, unary->min_count);
            } else if (unary->min_count == unary->max_count) {
                snprintf(buffer, capacity, "%s[%d]", operand_name, unary->min_count);
            } else {
                snprintf(buffer, capacity, "%s[%d,%d]", operand_name,
                    unary->min_count, unary->max_count);
            }
            return;
        }
        if (unary->op == OPERATOR_OPTIONAL) {
            snprintf(buffer, capacity, "%s?", operand_name);
            return;
        }
    }
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_BINARY) {
        const TypeBinary* binary = (const TypeBinary*)type;
        // All three set operators render in their source spelling. Only `|` did
        // before, so an `int & string` contract printed as the bare word "type"
        // and the diagnostic named neither the operator nor the operands
        // (LR02-9). `OPERATOR_OR` is the historical spelling of a type-level
        // `&` and renders the same way.
        const char* spelling =
            binary->op == OPERATOR_UNION ? " | " :
            (binary->op == OPERATOR_INTERSECT || binary->op == OPERATOR_OR) ? " & " :
            binary->op == OPERATOR_EXCLUDE ? " ! " : NULL;
        if (spelling) {
            char left_name[128];
            char right_name[128];
            lambda_type_format_name_inner(binary->left, left_name,
                sizeof(left_name), depth + 1);
            lambda_type_format_name_inner(binary->right, right_name,
                sizeof(right_name), depth + 1);
            snprintf(buffer, capacity, "%s%s%s", left_name, spelling, right_name);
            return;
        }
    }
    snprintf(buffer, capacity, "%s", type_contract_display_name(type));
}

void lambda_type_format_name(const Type* type, char* buffer, size_t capacity) {
    lambda_type_format_name_inner(type, buffer, capacity, 0);
}

static uint8_t contract_top_exclusions(Type* type) {
    if (type == &TYPE_ANY) return 0;
    if (type == &TYPE_ANY_NO_ERROR) return LAMBDA_TYPE_EXCLUDE_ERROR;
    if (type == &TYPE_ANY_NO_NULL) return LAMBDA_TYPE_EXCLUDE_NULL;
    if (type == &TYPE_ANY_NO_ERROR_OR_NULL) {
        return LAMBDA_TYPE_EXCLUDE_ERROR | LAMBDA_TYPE_EXCLUDE_NULL;
    }
    return UINT8_MAX;
}

static Type* contract_top_from_exclusions(uint8_t exclusions) {
    switch (exclusions) {
    case 0: return &TYPE_ANY;
    case LAMBDA_TYPE_EXCLUDE_ERROR: return &TYPE_ANY_NO_ERROR;
    case LAMBDA_TYPE_EXCLUDE_NULL: return &TYPE_ANY_NO_NULL;
    case LAMBDA_TYPE_EXCLUDE_ERROR | LAMBDA_TYPE_EXCLUDE_NULL:
        return &TYPE_ANY_NO_ERROR_OR_NULL;
    default: return NULL;
    }
}

static bool contract_same_atomic_type(Type* left, Type* right) {
    left = contract_unwrap_type(left);
    right = contract_unwrap_type(right);
    if (left == right) return true;
    if (!left || !right || left->type_id != right->type_id || left->kind != right->kind) {
        return false;
    }
    // Scalar TypeIds are canonical in the builder. Do not collapse map, array,
    // or extended types merely because their compact prefix happens to match.
    return left->kind == TYPE_KIND_SIMPLE && left->type_id != LMD_TYPE_MAP &&
        left->type_id != LMD_TYPE_ARRAY && left->type_id != LMD_TYPE_ELEMENT && left->type_id != LMD_TYPE_TYPE;
}

static bool contract_union_contains(Type* haystack, Type* needle) {
    haystack = contract_unwrap_type(haystack);
    if (contract_same_atomic_type(haystack, needle)) return true;
    if (!lambda_type_is_union(haystack)) return false;
    TypeBinary* binary = (TypeBinary*)haystack;
    return contract_union_contains(binary->left, needle) ||
        contract_union_contains(binary->right, needle);
}

// Native-lane projection of the one resolver (SCU8): true exactly when the
// contract has a raw register/slot representation the JIT may address.
bool lambda_type_lane_storage_desc(Type* type, LaneStorageDesc* out) {
    if (!out) return false;
    *out = {};
    if (!type) return false;
    LaneStorageDesc desc = lambda_lane_storage_desc_for(type);
    if (!desc.native) return false;
    *out = desc;
    return true;
}

Type* lambda_type_nullable_normalized(Pool* pool, Type* type) {
    type = contract_unwrap_type(type);
    if (!type || lambda_type_accepts_null(type) || lambda_type_accepts_error(type) ||
            !pool) {
        return type;
    }
    // A total read contributes absence, not a heterogeneous union. Preserve
    // the exact payload Type so the lane resolver can distinguish T from T?.
    TypeUnary* optional = (TypeUnary*)alloc_type_kind(pool, TYPE_KIND_UNARY,
        sizeof(TypeUnary));
    optional->operand = type;
    optional->op = OPERATOR_OPTIONAL;
    optional->min_count = 0;
    optional->max_count = 1;
    return (Type*)optional;
}

bool lambda_type_has_proven_error(Type* type) {
    type = contract_unwrap_type(type);
    if (!type || type->type_id == LMD_TYPE_ANY) return false;
    if (type->type_id == LMD_TYPE_ERROR) return true;
    if (!lambda_type_is_union(type)) return false;
    TypeBinary* binary = (TypeBinary*)type;
    return lambda_type_has_proven_error(binary->left) ||
        lambda_type_has_proven_error(binary->right);
}

Type* lambda_type_union_normalized(Pool* pool, Type* left, Type* right) {
    left = contract_unwrap_type(left);
    right = contract_unwrap_type(right);
    if (!left) return right;
    if (!right) return left;

    uint8_t left_top = contract_top_exclusions(left);
    uint8_t right_top = contract_top_exclusions(right);
    if (left_top != UINT8_MAX || right_top != UINT8_MAX) {
        if (left_top == UINT8_MAX) {
            left_top = 0;
            if (!lambda_type_accepts_error(left)) left_top |= LAMBDA_TYPE_EXCLUDE_ERROR;
            if (!lambda_type_accepts_null(left)) left_top |= LAMBDA_TYPE_EXCLUDE_NULL;
        }
        if (right_top == UINT8_MAX) {
            right_top = 0;
            if (!lambda_type_accepts_error(right)) right_top |= LAMBDA_TYPE_EXCLUDE_ERROR;
            if (!lambda_type_accepts_null(right)) right_top |= LAMBDA_TYPE_EXCLUDE_NULL;
        }
        return contract_top_from_exclusions(left_top & right_top);
    }
    if (contract_same_atomic_type(left, right) || contract_union_contains(left, right)) {
        return left;
    }
    if (contract_union_contains(right, left)) return right;
    if (!pool) return left;

    TypeBinary* result = (TypeBinary*)alloc_type_kind(pool, TYPE_KIND_BINARY,
        sizeof(TypeBinary));
    result->left = left;
    result->right = right;
    result->op = OPERATOR_UNION;
    return (Type*)result;
}

Type* lambda_type_remove_exclusions(Pool* pool, Type* type, uint8_t exclusions) {
    type = contract_unwrap_type(type);
    if (!type || exclusions == 0) return type;

    uint8_t top_exclusions = contract_top_exclusions(type);
    if (top_exclusions != UINT8_MAX) {
        return contract_top_from_exclusions(top_exclusions | exclusions);
    }
    if ((exclusions & LAMBDA_TYPE_EXCLUDE_ERROR) && type->type_id == LMD_TYPE_ERROR) {
        return NULL;
    }
    if ((exclusions & LAMBDA_TYPE_EXCLUDE_NULL) && type->type_id == LMD_TYPE_NULL) {
        return NULL;
    }
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_UNARY) {
        TypeUnary* unary = (TypeUnary*)type;
        if (unary->op == OPERATOR_OPTIONAL && (exclusions & LAMBDA_TYPE_EXCLUDE_NULL)) {
            return lambda_type_remove_exclusions(pool, unary->operand, exclusions);
        }
    }
    if (!lambda_type_is_union(type)) return type;

    TypeBinary* binary = (TypeBinary*)type;
    Type* left = lambda_type_remove_exclusions(pool, binary->left, exclusions);
    Type* right = lambda_type_remove_exclusions(pool, binary->right, exclusions);
    if (left == binary->left && right == binary->right) return type;
    return lambda_type_union_normalized(pool, left, right);
}

static bool contract_numeric_values_match(Item left, Item right) {
    LambdaNumericComparison comparison = lambda_numeric_compare(left, right);
    if (!comparison.valid || comparison.unordered || comparison.order != 0) return false;

    LambdaNumericRuntimePart left_part;
    LambdaNumericRuntimePart right_part;
    if (lambda_numeric_runtime_part(left, &left_part) &&
            lambda_numeric_runtime_part(right, &right_part) &&
            left_part.kind == LAMBDA_NUM_PART_FLOAT &&
            right_part.kind == LAMBDA_NUM_PART_FLOAT &&
            left_part.float_value == 0.0 && right_part.float_value == 0.0) {
        return signbit(left_part.float_value) == signbit(right_part.float_value);
    }
    return true;
}

static bool contract_numeric_is_integral(Item value) {
    LambdaNumericRuntimePart part;
    if (lambda_numeric_runtime_part(value, &part)) {
        if (part.kind != LAMBDA_NUM_PART_FLOAT) return true;
        return isfinite(part.float_value) && floor(part.float_value) == part.float_value;
    }
    if (get_type_id(value) != LMD_TYPE_DECIMAL) return false;
    Decimal* decimal = value.get_decimal();
    return decimal && decimal->dec_val && mpd_isinteger(decimal->dec_val);
}

static bool contract_numeric_to_uint64_exact(Item value, uint64_t* out) {
    if (!out) return false;
    LambdaNumericRuntimePart part;
    if (lambda_numeric_runtime_part(value, &part)) {
        if (part.kind == LAMBDA_NUM_PART_SIGNED) {
            if (part.signed_value < 0) return false;
            *out = (uint64_t)part.signed_value;
            return true;
        }
        if (part.kind == LAMBDA_NUM_PART_UNSIGNED) {
            *out = part.unsigned_value;
            return true;
        }
        double number = part.float_value;
        if (!isfinite(number) || number < 0.0 ||
                number >= 18446744073709551616.0 || floor(number) != number) {
            return false;
        }
        uint64_t integral = (uint64_t)number;
        if ((double)integral != number) return false;
        *out = integral;
        return true;
    }
    return decimal_to_uint64_exact(value, out);
}

static bool contract_numeric_admit_signed(Item value, LambdaNumericKind target,
        Item* converted) {
    if (target == LAMBDA_NUM_INT) {
        // v5 closes int at int53.  The shared IEEE poison remains a member,
        // but finite admission must reject sparse values before boxing would
        // silently turn them into inf.
        LambdaNumericRuntimePart int_part;
        if (!lambda_numeric_runtime_part(value, &int_part)) {
            // Decimal and other non-part carriers keep the exact-integral route.
            int64_t exact = 0;
            if (!contract_numeric_is_integral(value)) return false;
            if (!lambda_item_to_int64_exact(value, &exact)) return false;
            if (exact < INT53_MIN || exact > INT53_MAX) return false;
            converted->item = i2it(exact);
            return true;
        }
        if (lambda_item_is_merged_poison(value.item)) {
            *converted = value;
            return true;
        }
        if (int_part.kind == LAMBDA_NUM_PART_SIGNED) {
            if (int_part.signed_value < INT53_MIN || int_part.signed_value > INT53_MAX) return false;
            converted->item = i2it(int_part.signed_value);
            return true;
        }
        if (int_part.kind == LAMBDA_NUM_PART_UNSIGNED) {
            if (int_part.unsigned_value > (uint64_t)INT53_MAX) return false;
            converted->item = i2it((int64_t)int_part.unsigned_value);
            return true;
        }
        double numeric = int_part.float_value;
        if (!isfinite(numeric) || floor(numeric) != numeric ||
                numeric < (double)INT53_MIN || numeric > (double)INT53_MAX) return false;
        converted->item = lambda_int_box_double(numeric);
        return true;
    }
    int64_t integral = 0;
    if (!lambda_item_to_int64_exact(value, &integral)) return false;
    switch (target) {
    case LAMBDA_NUM_I64:
        *converted = box_int64_value(integral);
        return get_type_id(*converted) != LMD_TYPE_ERROR;
    case LAMBDA_NUM_I8:
        if (integral < INT8_MIN || integral > INT8_MAX) return false;
        converted->item = i8_to_item(integral);
        return true;
    case LAMBDA_NUM_I16:
        if (integral < INT16_MIN || integral > INT16_MAX) return false;
        converted->item = i16_to_item(integral);
        return true;
    case LAMBDA_NUM_I32:
        if (integral < INT32_MIN || integral > INT32_MAX) return false;
        converted->item = i32_to_item(integral);
        return true;
    default:
        return false;
    }
}

static bool contract_numeric_admit_unsigned(Item value, LambdaNumericKind target,
        Item* converted) {
    uint64_t integral = 0;
    if (!contract_numeric_to_uint64_exact(value, &integral)) return false;
    switch (target) {
    case LAMBDA_NUM_U64:
        *converted = box_uint64_value(integral);
        return get_type_id(*converted) != LMD_TYPE_ERROR;
    case LAMBDA_NUM_U8:
        if (integral > UINT8_MAX) return false;
        converted->item = u8_to_item(integral);
        return true;
    case LAMBDA_NUM_U16:
        if (integral > UINT16_MAX) return false;
        converted->item = u16_to_item(integral);
        return true;
    case LAMBDA_NUM_U32:
        if (integral > UINT32_MAX) return false;
        converted->item = u32_to_item(integral);
        return true;
    default:
        return false;
    }
}

static bool contract_numeric_admit_float(Item value, LambdaNumericKind target,
        Item* converted) {
    LambdaNumericKind source = lambda_numeric_kind_from_item(value);
    if (source == target) {
        *converted = value;
        return true;
    }

    double number = it2d(value);
    if (target == LAMBDA_NUM_FLOAT) {
        *converted = push_d(number);
    } else if (target == LAMBDA_NUM_F32) {
        converted->item = f32_to_item((float)number);
    } else if (target == LAMBDA_NUM_F16) {
        converted->item = f16_to_item((float)number);
    } else {
        return false;
    }
    return get_type_id(*converted) != LMD_TYPE_ERROR &&
        contract_numeric_values_match(value, *converted);
}

static bool contract_numeric_admit_decimal(Item value, Item* converted) {
    if (get_type_id(value) == LMD_TYPE_DECIMAL) {
        *converted = value;
        return true;
    }

    // Decimal materialization allocates. Keep the source rooted so a collection
    // during the conversion cannot leave the comparison or returned candidate
    // pointing at a reclaimed scalar payload.
    RootFrame roots(1);
    Rooted<Item> rooted_value(roots, value);
    LambdaNumericRuntimePart part;
    if (!lambda_numeric_runtime_part(rooted_value.get(), &part)) return false;
    Item result = ItemError;
    if (part.kind == LAMBDA_NUM_PART_SIGNED) {
        result = decimal_from_int64(part.signed_value);
    } else if (part.kind == LAMBDA_NUM_PART_UNSIGNED) {
        result = decimal_from_uint64(part.unsigned_value);
    } else if (isfinite(part.float_value)) {
        result = decimal_from_double(part.float_value);
    }
    if (get_type_id(result) == LMD_TYPE_ERROR) return false;
    *converted = result;
    return true;
}

bool lambda_numeric_boundary_admit(Item value, Type* target, Item* converted) {
    if (!converted) return false;
    target = contract_unwrap_type(target);
    LambdaNumericKind source_kind = lambda_numeric_kind_from_item(value);
    LambdaNumericKind target_kind = lambda_numeric_kind_from_type(target);
    if (source_kind == LAMBDA_NUM_INVALID || target_kind == LAMBDA_NUM_INVALID) return false;

    if (target == &TYPE_NUMBER) {
        *converted = value;
        return true;
    }
    if (target_kind == LAMBDA_NUM_INTEGER) {
        if (!contract_numeric_is_integral(value)) return false;
        // The inferred bigint-decimal carrier also denotes abstract `integer`.
        // Retain the exact value instead of forcing it through int64 or
        // fabricating a new observable Item tag at the boundary.
        *converted = value;
        return true;
    }
    if (target_kind == LAMBDA_NUM_DECIMAL) {
        return contract_numeric_admit_decimal(value, converted);
    }
    if (target_kind == LAMBDA_NUM_FLOAT || target_kind == LAMBDA_NUM_F16 ||
            target_kind == LAMBDA_NUM_F32) {
        return contract_numeric_admit_float(value, target_kind, converted);
    }
    if (target_kind == LAMBDA_NUM_U8 || target_kind == LAMBDA_NUM_U16 ||
            target_kind == LAMBDA_NUM_U32 || target_kind == LAMBDA_NUM_U64) {
        return contract_numeric_admit_unsigned(value, target_kind, converted);
    }
    return contract_numeric_admit_signed(value, target_kind, converted);
}
