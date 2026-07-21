#ifndef LAMBDA_NUMBER_RUNTIME_HPP
#define LAMBDA_NUMBER_RUNTIME_HPP

#include "../lambda-number.hpp"
#include "../lambda-data.hpp"
#include "../lambda-decimal.hpp"
#include <math.h>

static inline LambdaNumericKind lambda_numeric_kind_from_item_type(
        Item item, TypeId type) {
    switch (type) {
    case LMD_TYPE_INT: return LAMBDA_NUM_INT;
    case LMD_TYPE_INT64: return LAMBDA_NUM_I64;
    case LMD_TYPE_UINT64: return LAMBDA_NUM_U64;
    case LMD_TYPE_FLOAT: case LMD_TYPE_FLOAT64: return LAMBDA_NUM_FLOAT;
    case LMD_TYPE_NUM_SIZED:
        return lambda_numeric_kind_from_sized_type(item.get_num_type());
    case LMD_TYPE_DECIMAL: {
        Decimal* decimal = item.get_decimal();
        return decimal && decimal->unlimited == DECIMAL_BIGINT ?
            LAMBDA_NUM_INTEGER : LAMBDA_NUM_DECIMAL;
    }
    default:
        return LAMBDA_NUM_INVALID;
    }
}

static inline LambdaNumericKind lambda_numeric_kind_from_item(Item item) {
    return lambda_numeric_kind_from_item_type(item, get_type_id(item));
}

typedef enum LambdaNumericRuntimePartKind {
    LAMBDA_NUM_PART_SIGNED = 0,
    LAMBDA_NUM_PART_UNSIGNED,
    LAMBDA_NUM_PART_FLOAT,
} LambdaNumericRuntimePartKind;

typedef struct LambdaNumericRuntimePart {
    LambdaNumericRuntimePartKind kind;
    int64_t signed_value;
    uint64_t unsigned_value;
    double float_value;
} LambdaNumericRuntimePart;

typedef struct LambdaNumericComparison {
    uint8_t valid;
    uint8_t unordered;
    int8_t order;
} LambdaNumericComparison;

static inline uint8_t lambda_numeric_runtime_part(
        Item item, LambdaNumericRuntimePart* part) {
    if (!part) return 0;
    LambdaNumericKind kind = lambda_numeric_kind_from_item(item);
    switch (kind) {
    case LAMBDA_NUM_INT:
        part->kind = LAMBDA_NUM_PART_SIGNED;
        part->signed_value = item.get_int56();
        return 1;
    case LAMBDA_NUM_I8:
        part->kind = LAMBDA_NUM_PART_SIGNED;
        part->signed_value = item.get_i8();
        return 1;
    case LAMBDA_NUM_I16:
        part->kind = LAMBDA_NUM_PART_SIGNED;
        part->signed_value = item.get_i16();
        return 1;
    case LAMBDA_NUM_I32:
        part->kind = LAMBDA_NUM_PART_SIGNED;
        part->signed_value = item.get_i32();
        return 1;
    case LAMBDA_NUM_I64:
        part->kind = LAMBDA_NUM_PART_SIGNED;
        part->signed_value = item.get_int64();
        return 1;
    case LAMBDA_NUM_U8:
        part->kind = LAMBDA_NUM_PART_UNSIGNED;
        part->unsigned_value = item.get_u8();
        return 1;
    case LAMBDA_NUM_U16:
        part->kind = LAMBDA_NUM_PART_UNSIGNED;
        part->unsigned_value = item.get_u16();
        return 1;
    case LAMBDA_NUM_U32:
        part->kind = LAMBDA_NUM_PART_UNSIGNED;
        part->unsigned_value = item.get_u32();
        return 1;
    case LAMBDA_NUM_U64:
        part->kind = LAMBDA_NUM_PART_UNSIGNED;
        part->unsigned_value = item.get_uint64();
        return 1;
    case LAMBDA_NUM_FLOAT:
        part->kind = LAMBDA_NUM_PART_FLOAT;
        part->float_value = item.get_double();
        return 1;
    case LAMBDA_NUM_F16:
        part->kind = LAMBDA_NUM_PART_FLOAT;
        part->float_value = item.get_f16();
        return 1;
    case LAMBDA_NUM_F32:
        part->kind = LAMBDA_NUM_PART_FLOAT;
        part->float_value = item.get_f32();
        return 1;
    default:
        return 0;
    }
}

// Extract a mathematical integer only when it fits exactly in int64. This is
// the common boundary for ranges, indices, dimensions, axes, and count-like
// APIs; type-directed i64/u64 arithmetic may legitimately reach it as the
// semantic `integer` carrier.
static inline uint8_t lambda_item_to_int64_exact(Item item, int64_t* out) {
    if (!out) return 0;
    LambdaNumericRuntimePart part;
    if (lambda_numeric_runtime_part(item, &part)) {
        if (part.kind == LAMBDA_NUM_PART_SIGNED) {
            *out = part.signed_value;
            return 1;
        }
        if (part.kind == LAMBDA_NUM_PART_UNSIGNED) {
            if (part.unsigned_value > (uint64_t)INT64_MAX) return 0;
            *out = (int64_t)part.unsigned_value;
            return 1;
        }
        double value = part.float_value;
        if (!isfinite(value) || value < -9223372036854775808.0 ||
                value >= 9223372036854775808.0) return 0;
        int64_t integral = (int64_t)value;
        if ((double)integral != value) return 0;
        *out = integral;
        return 1;
    }
    return decimal_to_int64_exact(item, out) ? 1 : 0;
}

// Compare numeric values without using binary64 as a common integer carrier.
// Mixed float/integer and decimal cases use Lambda's canonical decimal
// conversion, which is the same exact comparison relation used by equality.
static inline LambdaNumericComparison lambda_numeric_compare(Item left, Item right) {
    LambdaNumericComparison result = {0, 0, 0};
    LambdaNumericKind left_kind = lambda_numeric_kind_from_item(left);
    LambdaNumericKind right_kind = lambda_numeric_kind_from_item(right);
    if (left_kind == LAMBDA_NUM_INVALID || right_kind == LAMBDA_NUM_INVALID) return result;

    LambdaNumericRuntimePart left_part;
    LambdaNumericRuntimePart right_part;
    uint8_t left_simple = lambda_numeric_runtime_part(left, &left_part);
    uint8_t right_simple = lambda_numeric_runtime_part(right, &right_part);
    result.valid = 1;

    if (left_simple && left_part.kind == LAMBDA_NUM_PART_FLOAT &&
        isnan(left_part.float_value)) {
        result.unordered = 1;
        return result;
    }
    if (right_simple && right_part.kind == LAMBDA_NUM_PART_FLOAT &&
        isnan(right_part.float_value)) {
        result.unordered = 1;
        return result;
    }

    if (!left_simple || !right_simple ||
        left_part.kind == LAMBDA_NUM_PART_FLOAT ||
        right_part.kind == LAMBDA_NUM_PART_FLOAT) {
        int cmp = decimal_cmp_items(left, right);
        result.order = cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
        return result;
    }

    if (left_part.kind == LAMBDA_NUM_PART_SIGNED &&
        right_part.kind == LAMBDA_NUM_PART_SIGNED) {
        result.order = left_part.signed_value < right_part.signed_value ? -1 :
            left_part.signed_value > right_part.signed_value ? 1 : 0;
    } else if (left_part.kind == LAMBDA_NUM_PART_UNSIGNED &&
               right_part.kind == LAMBDA_NUM_PART_UNSIGNED) {
        result.order = left_part.unsigned_value < right_part.unsigned_value ? -1 :
            left_part.unsigned_value > right_part.unsigned_value ? 1 : 0;
    } else if (left_part.kind == LAMBDA_NUM_PART_SIGNED) {
        result.order = left_part.signed_value < 0 ? -1 :
            (uint64_t)left_part.signed_value < right_part.unsigned_value ? -1 :
            (uint64_t)left_part.signed_value > right_part.unsigned_value ? 1 : 0;
    } else {
        result.order = right_part.signed_value < 0 ? 1 :
            left_part.unsigned_value < (uint64_t)right_part.signed_value ? -1 :
            left_part.unsigned_value > (uint64_t)right_part.signed_value ? 1 : 0;
    }
    return result;
}

#endif
