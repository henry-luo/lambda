#ifndef LAMBDA_NUMBER_HPP
#define LAMBDA_NUMBER_HPP

#include "lambda.h"

// Numeric promotion is a language rule, not a TypeId ordering rule.  Keep this
// descriptor independent of Item storage so the evaluator and both transpilers
// consume the same type-directed decision.
typedef enum LambdaNumericKind {
    LAMBDA_NUM_INVALID = 0,
    LAMBDA_NUM_INT,
    LAMBDA_NUM_INTEGER,
    LAMBDA_NUM_FLOAT,
    LAMBDA_NUM_DECIMAL,
    LAMBDA_NUM_I8,
    LAMBDA_NUM_I16,
    LAMBDA_NUM_I32,
    LAMBDA_NUM_I64,
    LAMBDA_NUM_U8,
    LAMBDA_NUM_U16,
    LAMBDA_NUM_U32,
    LAMBDA_NUM_U64,
    LAMBDA_NUM_F16,
    LAMBDA_NUM_F32,
} LambdaNumericKind;

typedef enum LambdaNumericOpFamily {
    LAMBDA_NUM_OP_ADD = 0,
    LAMBDA_NUM_OP_SUB,
    LAMBDA_NUM_OP_MUL,
    LAMBDA_NUM_OP_TRUE_DIV,
    LAMBDA_NUM_OP_IDIV,
    LAMBDA_NUM_OP_MOD,
    LAMBDA_NUM_OP_BITWISE,
    LAMBDA_NUM_OP_SHIFT,
} LambdaNumericOpFamily;

typedef enum LambdaNumericOverflow {
    LAMBDA_NUM_OVERFLOW_EXACT = 0,
    LAMBDA_NUM_OVERFLOW_INT_TO_FLOAT,
    LAMBDA_NUM_OVERFLOW_SIZED_WRAP,
    LAMBDA_NUM_OVERFLOW_IEEE,
} LambdaNumericOverflow;

typedef struct LambdaNumericDecision {
    uint8_t valid;
    LambdaNumericKind left_domain;
    LambdaNumericKind right_domain;
    LambdaNumericKind result;
    LambdaNumericOverflow overflow;
} LambdaNumericDecision;

static inline uint8_t lambda_numeric_is_sized_integer(LambdaNumericKind kind) {
    return kind >= LAMBDA_NUM_I8 && kind <= LAMBDA_NUM_U64;
}

static inline uint8_t lambda_numeric_is_sized_float(LambdaNumericKind kind) {
    return kind == LAMBDA_NUM_F16 || kind == LAMBDA_NUM_F32;
}

static inline uint8_t lambda_numeric_is_sized(LambdaNumericKind kind) {
    return lambda_numeric_is_sized_integer(kind) || lambda_numeric_is_sized_float(kind);
}

static inline int lambda_numeric_sized_bits(LambdaNumericKind kind) {
    switch (kind) {
    case LAMBDA_NUM_I8: case LAMBDA_NUM_U8: return 8;
    case LAMBDA_NUM_I16: case LAMBDA_NUM_U16: return 16;
    case LAMBDA_NUM_I32: case LAMBDA_NUM_U32: return 32;
    case LAMBDA_NUM_I64: case LAMBDA_NUM_U64: return 64;
    default: return 0;
    }
}

static inline uint8_t lambda_numeric_sized_is_unsigned(LambdaNumericKind kind) {
    return kind == LAMBDA_NUM_U8 || kind == LAMBDA_NUM_U16 ||
           kind == LAMBDA_NUM_U32 || kind == LAMBDA_NUM_U64;
}

static inline LambdaNumericKind lambda_numeric_sized_kind(uint8_t is_unsigned, int bits) {
    if (is_unsigned) {
        if (bits <= 8) return LAMBDA_NUM_U8;
        if (bits <= 16) return LAMBDA_NUM_U16;
        if (bits <= 32) return LAMBDA_NUM_U32;
        return LAMBDA_NUM_U64;
    }
    if (bits <= 8) return LAMBDA_NUM_I8;
    if (bits <= 16) return LAMBDA_NUM_I16;
    if (bits <= 32) return LAMBDA_NUM_I32;
    return LAMBDA_NUM_I64;
}

static inline LambdaNumericKind lambda_numeric_sized_integer_result(
        LambdaNumericKind left, LambdaNumericKind right) {
    int left_bits = lambda_numeric_sized_bits(left);
    int right_bits = lambda_numeric_sized_bits(right);
    uint8_t left_unsigned = lambda_numeric_sized_is_unsigned(left);
    uint8_t right_unsigned = lambda_numeric_sized_is_unsigned(right);
    if (left_unsigned == right_unsigned) {
        return lambda_numeric_sized_kind(left_unsigned,
            left_bits > right_bits ? left_bits : right_bits);
    }

    int signed_bits = left_unsigned ? right_bits : left_bits;
    int unsigned_bits = left_unsigned ? left_bits : right_bits;
    if (signed_bits > unsigned_bits) {
        return lambda_numeric_sized_kind(0, signed_bits);
    }
    if (unsigned_bits < 64) {
        int result_bits = unsigned_bits < 8 ? 8 :
            unsigned_bits < 16 ? 16 : unsigned_bits < 32 ? 32 : 64;
        if (result_bits < signed_bits) result_bits = signed_bits;
        return lambda_numeric_sized_kind(0, result_bits);
    }
    return LAMBDA_NUM_U64;
}

static inline LambdaNumericKind lambda_numeric_enter_semantic_domain(
        LambdaNumericKind kind) {
    switch (kind) {
    case LAMBDA_NUM_I8: case LAMBDA_NUM_I16: case LAMBDA_NUM_I32:
    case LAMBDA_NUM_U8: case LAMBDA_NUM_U16: case LAMBDA_NUM_U32:
        return LAMBDA_NUM_INT;
    case LAMBDA_NUM_I64: case LAMBDA_NUM_U64:
        return LAMBDA_NUM_INTEGER;
    case LAMBDA_NUM_F16: case LAMBDA_NUM_F32:
        return LAMBDA_NUM_FLOAT;
    default:
        return kind;
    }
}

static inline LambdaNumericKind lambda_numeric_semantic_join(
        LambdaNumericKind left, LambdaNumericKind right) {
    if (left == LAMBDA_NUM_DECIMAL || right == LAMBDA_NUM_DECIMAL) {
        return LAMBDA_NUM_DECIMAL;
    }
    if (left == LAMBDA_NUM_INTEGER || right == LAMBDA_NUM_INTEGER) {
        // Binary floats cannot exactly contain the integer domain, so the
        // complete common domain is decimal rather than float.
        if (left == LAMBDA_NUM_FLOAT || right == LAMBDA_NUM_FLOAT) {
            return LAMBDA_NUM_DECIMAL;
        }
        return LAMBDA_NUM_INTEGER;
    }
    if (left == LAMBDA_NUM_FLOAT || right == LAMBDA_NUM_FLOAT) {
        return LAMBDA_NUM_FLOAT;
    }
    if (left == LAMBDA_NUM_INT && right == LAMBDA_NUM_INT) {
        return LAMBDA_NUM_INT;
    }
    return LAMBDA_NUM_INVALID;
}

static inline LambdaNumericDecision lambda_numeric_classify(
        LambdaNumericOpFamily op, LambdaNumericKind left, LambdaNumericKind right) {
    LambdaNumericDecision decision = {
        0, LAMBDA_NUM_INVALID, LAMBDA_NUM_INVALID,
        LAMBDA_NUM_INVALID, LAMBDA_NUM_OVERFLOW_EXACT
    };
    if (left == LAMBDA_NUM_INVALID || right == LAMBDA_NUM_INVALID) return decision;

    uint8_t common_left = left == LAMBDA_NUM_INT || left == LAMBDA_NUM_FLOAT;
    uint8_t common_right = right == LAMBDA_NUM_INT || right == LAMBDA_NUM_FLOAT;
    if (common_left && common_right) {
        LambdaNumericKind joined =
            (left == LAMBDA_NUM_FLOAT || right == LAMBDA_NUM_FLOAT) ?
            LAMBDA_NUM_FLOAT : LAMBDA_NUM_INT;
        if (joined == LAMBDA_NUM_FLOAT &&
            (op == LAMBDA_NUM_OP_IDIV || op == LAMBDA_NUM_OP_MOD ||
             op == LAMBDA_NUM_OP_BITWISE || op == LAMBDA_NUM_OP_SHIFT)) {
            return decision;
        }
        decision.valid = 1;
        decision.left_domain = left;
        decision.right_domain = right;
        decision.result = op == LAMBDA_NUM_OP_TRUE_DIV && joined == LAMBDA_NUM_INT ?
            LAMBDA_NUM_FLOAT : joined;
        decision.overflow = decision.result == LAMBDA_NUM_INT ?
            LAMBDA_NUM_OVERFLOW_INT_TO_FLOAT : LAMBDA_NUM_OVERFLOW_IEEE;
        return decision;
    }

    if (op == LAMBDA_NUM_OP_SHIFT) {
        LambdaNumericKind count_domain = lambda_numeric_enter_semantic_domain(right);
        if (count_domain != LAMBDA_NUM_INT && count_domain != LAMBDA_NUM_INTEGER) {
            return decision;
        }
        LambdaNumericKind value_domain = lambda_numeric_enter_semantic_domain(left);
        if (!lambda_numeric_is_sized_integer(left) &&
                value_domain != LAMBDA_NUM_INT && value_domain != LAMBDA_NUM_INTEGER) {
            return decision;
        }
        // The right operand is a shift count, not a peer arithmetic value.  Go
        // therefore preserves the left operand's type regardless of count type.
        decision.valid = 1;
        decision.left_domain = lambda_numeric_is_sized_integer(left) ? left : value_domain;
        decision.right_domain = count_domain;
        decision.result = decision.left_domain;
        decision.overflow = lambda_numeric_is_sized_integer(left) ?
            LAMBDA_NUM_OVERFLOW_SIZED_WRAP :
            value_domain == LAMBDA_NUM_INT ? LAMBDA_NUM_OVERFLOW_INT_TO_FLOAT :
            LAMBDA_NUM_OVERFLOW_EXACT;
        return decision;
    }

    uint8_t both_sized_integers = lambda_numeric_is_sized_integer(left) &&
                                  lambda_numeric_is_sized_integer(right);
    if (both_sized_integers && op != LAMBDA_NUM_OP_TRUE_DIV) {
        decision.valid = 1;
        decision.left_domain = left;
        decision.right_domain = right;
        decision.result = lambda_numeric_sized_integer_result(left, right);
        decision.overflow = LAMBDA_NUM_OVERFLOW_SIZED_WRAP;
        return decision;
    }

    uint8_t both_sized_floats = lambda_numeric_is_sized_float(left) &&
                                lambda_numeric_is_sized_float(right);
    if (both_sized_floats && op != LAMBDA_NUM_OP_IDIV &&
        op != LAMBDA_NUM_OP_MOD && op != LAMBDA_NUM_OP_BITWISE &&
        op != LAMBDA_NUM_OP_SHIFT) {
        decision.valid = 1;
        decision.left_domain = left;
        decision.right_domain = right;
        decision.result = (left == LAMBDA_NUM_F32 || right == LAMBDA_NUM_F32) ?
            LAMBDA_NUM_F32 : LAMBDA_NUM_F16;
        decision.overflow = LAMBDA_NUM_OVERFLOW_IEEE;
        return decision;
    }

    decision.left_domain = lambda_numeric_enter_semantic_domain(left);
    decision.right_domain = lambda_numeric_enter_semantic_domain(right);
    LambdaNumericKind joined = lambda_numeric_semantic_join(
        decision.left_domain, decision.right_domain);
    if (joined == LAMBDA_NUM_INVALID) return decision;

    if (op == LAMBDA_NUM_OP_BITWISE || op == LAMBDA_NUM_OP_SHIFT) {
        if (joined != LAMBDA_NUM_INT && joined != LAMBDA_NUM_INTEGER) return decision;
        decision.result = joined;
    } else if (op == LAMBDA_NUM_OP_IDIV || op == LAMBDA_NUM_OP_MOD) {
        if (joined == LAMBDA_NUM_FLOAT) return decision;
        decision.result = joined;
    } else if (op == LAMBDA_NUM_OP_TRUE_DIV) {
        decision.result = joined == LAMBDA_NUM_INT ? LAMBDA_NUM_FLOAT :
                          joined == LAMBDA_NUM_INTEGER ? LAMBDA_NUM_DECIMAL : joined;
    } else {
        decision.result = joined;
    }

    decision.valid = 1;
    decision.overflow = decision.result == LAMBDA_NUM_INT ?
        LAMBDA_NUM_OVERFLOW_INT_TO_FLOAT :
        decision.result == LAMBDA_NUM_FLOAT ? LAMBDA_NUM_OVERFLOW_IEEE :
        LAMBDA_NUM_OVERFLOW_EXACT;
    return decision;
}

static inline LambdaNumericKind lambda_numeric_kind_from_sized_type(NumSizedType type) {
    switch (type) {
    case NUM_INT8: return LAMBDA_NUM_I8;
    case NUM_INT16: return LAMBDA_NUM_I16;
    case NUM_INT32: return LAMBDA_NUM_I32;
    case NUM_UINT8: return LAMBDA_NUM_U8;
    case NUM_UINT16: return LAMBDA_NUM_U16;
    case NUM_UINT32: return LAMBDA_NUM_U32;
    case NUM_FLOAT16: return LAMBDA_NUM_F16;
    case NUM_FLOAT32: return LAMBDA_NUM_F32;
    default: return LAMBDA_NUM_INVALID;
    }
}

static inline NumSizedType lambda_numeric_kind_to_sized_type(LambdaNumericKind kind) {
    switch (kind) {
    case LAMBDA_NUM_I8: return NUM_INT8;
    case LAMBDA_NUM_I16: return NUM_INT16;
    case LAMBDA_NUM_I32: return NUM_INT32;
    case LAMBDA_NUM_U8: return NUM_UINT8;
    case LAMBDA_NUM_U16: return NUM_UINT16;
    case LAMBDA_NUM_U32: return NUM_UINT32;
    case LAMBDA_NUM_F16: return NUM_FLOAT16;
    case LAMBDA_NUM_F32: return NUM_FLOAT32;
    default: return NUM_INT8;
    }
}

static inline LambdaNumericKind lambda_numeric_kind_from_elem_type(
        ArrayNumElemType elem_type) {
    switch (elem_type) {
    case ELEM_INT: return LAMBDA_NUM_INT;
    case ELEM_FLOAT64: return LAMBDA_NUM_FLOAT;
    case ELEM_INT8: return LAMBDA_NUM_I8;
    case ELEM_INT16: return LAMBDA_NUM_I16;
    case ELEM_INT32: return LAMBDA_NUM_I32;
    case ELEM_INT64: return LAMBDA_NUM_I64;
    case ELEM_UINT8: case ELEM_UINT8_CLAMPED: return LAMBDA_NUM_U8;
    case ELEM_UINT16: return LAMBDA_NUM_U16;
    case ELEM_UINT32: return LAMBDA_NUM_U32;
    case ELEM_UINT64: return LAMBDA_NUM_U64;
    case ELEM_FLOAT16: return LAMBDA_NUM_F16;
    case ELEM_FLOAT32: return LAMBDA_NUM_F32;
    default: return LAMBDA_NUM_INVALID;
    }
}

static inline uint8_t lambda_numeric_kind_to_elem_type(
        LambdaNumericKind kind, ArrayNumElemType* elem_type) {
    if (!elem_type) return 0;
    switch (kind) {
    case LAMBDA_NUM_INT: *elem_type = ELEM_INT; return 1;
    case LAMBDA_NUM_FLOAT: *elem_type = ELEM_FLOAT64; return 1;
    case LAMBDA_NUM_I8: *elem_type = ELEM_INT8; return 1;
    case LAMBDA_NUM_I16: *elem_type = ELEM_INT16; return 1;
    case LAMBDA_NUM_I32: *elem_type = ELEM_INT32; return 1;
    case LAMBDA_NUM_I64: *elem_type = ELEM_INT64; return 1;
    case LAMBDA_NUM_U8: *elem_type = ELEM_UINT8; return 1;
    case LAMBDA_NUM_U16: *elem_type = ELEM_UINT16; return 1;
    case LAMBDA_NUM_U32: *elem_type = ELEM_UINT32; return 1;
    case LAMBDA_NUM_U64: *elem_type = ELEM_UINT64; return 1;
    case LAMBDA_NUM_F16: *elem_type = ELEM_FLOAT16; return 1;
    case LAMBDA_NUM_F32: *elem_type = ELEM_FLOAT32; return 1;
    default: return 0;
    }
}

static inline uint8_t lambda_numeric_kind_exactly_embeds(
        LambdaNumericKind source, LambdaNumericKind destination) {
    if (source == destination) return 1;
    if (source == LAMBDA_NUM_INVALID || destination == LAMBDA_NUM_INVALID) return 0;

    if (lambda_numeric_is_sized_integer(source) &&
        lambda_numeric_is_sized_integer(destination)) {
        int source_bits = lambda_numeric_sized_bits(source);
        int destination_bits = lambda_numeric_sized_bits(destination);
        uint8_t source_unsigned = lambda_numeric_sized_is_unsigned(source);
        uint8_t destination_unsigned = lambda_numeric_sized_is_unsigned(destination);
        if (source_unsigned == destination_unsigned) return source_bits <= destination_bits;
        if (source_unsigned && !destination_unsigned) return source_bits < destination_bits;
        return 0;
    }

    if (lambda_numeric_is_sized_integer(source)) {
        int bits = lambda_numeric_sized_bits(source);
        if (destination == LAMBDA_NUM_INT) return bits < 64;
        if (destination == LAMBDA_NUM_INTEGER || destination == LAMBDA_NUM_DECIMAL) return 1;
        if (destination == LAMBDA_NUM_FLOAT) return bits <= 32;
        if (destination == LAMBDA_NUM_F16) return bits <= 8;
        if (destination == LAMBDA_NUM_F32) return bits <= 16;
    }
    if (source == LAMBDA_NUM_INT) {
        return destination == LAMBDA_NUM_I64 || destination == LAMBDA_NUM_INTEGER ||
               destination == LAMBDA_NUM_FLOAT || destination == LAMBDA_NUM_DECIMAL;
    }
    if (source == LAMBDA_NUM_INTEGER) return destination == LAMBDA_NUM_DECIMAL;
    if (source == LAMBDA_NUM_F16) {
        return destination == LAMBDA_NUM_F32 || destination == LAMBDA_NUM_FLOAT ||
               destination == LAMBDA_NUM_DECIMAL;
    }
    if (source == LAMBDA_NUM_F32 || source == LAMBDA_NUM_FLOAT) {
        return destination == LAMBDA_NUM_FLOAT || destination == LAMBDA_NUM_DECIMAL;
    }
    return 0;
}

#endif
