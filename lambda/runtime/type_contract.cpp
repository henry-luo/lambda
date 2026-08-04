#include "type_contract.hpp"
#include "lambda-number-types.hpp"
#include "lambda-number-runtime.hpp"
#include "lambda-root-frame.hpp"
#include <mpdecimal.h>
#include <stdio.h>

static Type* contract_unwrap_type(Type* type) {
    while (type && type->type_id == LMD_TYPE_TYPE && !type_is_global_meta_type(type) &&
            type->kind == TYPE_KIND_SIMPLE) {
        // The three global meta-types are compact Type values; inspect `kind`
        // only after excluding them so contract analysis never reads past them.
        Type* inner = ((TypeType*)type)->type;
        if (!inner) break;
        type = inner;
    }
    return type;
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
    if (type->type_id == LMD_TYPE_MAP) {
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
        if (unary->op == OPERATOR_REPEAT) {
            snprintf(buffer, capacity, "%s[]", operand_name);
            return;
        }
        if (unary->op == OPERATOR_OPTIONAL) {
            snprintf(buffer, capacity, "%s?", operand_name);
            return;
        }
    }
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_BINARY) {
        const TypeBinary* binary = (const TypeBinary*)type;
        if (binary->op == OPERATOR_UNION) {
            char left_name[128];
            char right_name[128];
            lambda_type_format_name_inner(binary->left, left_name,
                sizeof(left_name), depth + 1);
            lambda_type_format_name_inner(binary->right, right_name,
                sizeof(right_name), depth + 1);
            snprintf(buffer, capacity, "%s | %s", left_name, right_name);
            return;
        }
    }
    snprintf(buffer, capacity, "%s", type_contract_display_name(type));
}

void lambda_type_format_name(const Type* type, char* buffer, size_t capacity) {
    lambda_type_format_name_inner(type, buffer, capacity, 0);
}

static bool contract_is_union(Type* type) {
    return type && type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_BINARY &&
        ((TypeBinary*)type)->op == OPERATOR_UNION;
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
        left->type_id != LMD_TYPE_ARRAY && left->type_id != LMD_TYPE_ELEMENT &&
        left->type_id != LMD_TYPE_OBJECT && left->type_id != LMD_TYPE_TYPE;
}

static bool contract_union_contains(Type* haystack, Type* needle) {
    haystack = contract_unwrap_type(haystack);
    if (contract_same_atomic_type(haystack, needle)) return true;
    if (!contract_is_union(haystack)) return false;
    TypeBinary* binary = (TypeBinary*)haystack;
    return contract_union_contains(binary->left, needle) ||
        contract_union_contains(binary->right, needle);
}

bool lambda_type_accepts_error(Type* type) {
    type = contract_unwrap_type(type);
    if (!type || type_is_any_without_error(type)) return false;
    if (type->type_id == LMD_TYPE_ANY || type->type_id == LMD_TYPE_ERROR) return true;
    if (!contract_is_union(type)) return false;
    TypeBinary* binary = (TypeBinary*)type;
    return lambda_type_accepts_error(binary->left) || lambda_type_accepts_error(binary->right);
}

bool lambda_type_accepts_null(Type* type) {
    type = contract_unwrap_type(type);
    if (!type || type_is_any_without_null(type)) return false;
    if (type->type_id == LMD_TYPE_ANY || type->type_id == LMD_TYPE_NULL) return true;
    if (type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_UNARY) {
        TypeUnary* unary = (TypeUnary*)type;
        if (unary->op == OPERATOR_OPTIONAL) return true;
    }
    if (!contract_is_union(type)) return false;
    TypeBinary* binary = (TypeBinary*)type;
    return lambda_type_accepts_null(binary->left) || lambda_type_accepts_null(binary->right);
}

bool lambda_type_has_proven_error(Type* type) {
    type = contract_unwrap_type(type);
    if (!type || type->type_id == LMD_TYPE_ANY) return false;
    if (type->type_id == LMD_TYPE_ERROR) return true;
    if (!contract_is_union(type)) return false;
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
    if (!contract_is_union(type)) return type;

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
