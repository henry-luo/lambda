#ifndef LAMBDA_NUMBER_TYPES_HPP
#define LAMBDA_NUMBER_TYPES_HPP

#include "lambda-number.hpp"
#include "../lambda-data.hpp"

static inline LambdaNumericKind lambda_numeric_kind_from_type(const Type* type) {
    if (!type) return LAMBDA_NUM_INVALID;
    if (type == &TYPE_INTEGER || type == &TYPE_INTEGER_VALUE) return LAMBDA_NUM_INTEGER;
    switch (type->type_id) {
    case LMD_TYPE_INT: return LAMBDA_NUM_INT;
    case LMD_TYPE_INT64: return LAMBDA_NUM_I64;
    case LMD_TYPE_UINT64: return LAMBDA_NUM_U64;
    case LMD_TYPE_FLOAT: case LMD_TYPE_FLOAT64: return LAMBDA_NUM_FLOAT;
    case LMD_TYPE_NUM_SIZED:
        return lambda_numeric_kind_from_sized_type(type_num_sized_kind(type));
    case LMD_TYPE_DECIMAL:
        if ((type->is_literal || type->is_const) &&
            ((const TypeDecimal*)type)->decimal &&
            ((const TypeDecimal*)type)->decimal->unlimited == DECIMAL_BIGINT) {
            return LAMBDA_NUM_INTEGER;
        }
        return LAMBDA_NUM_DECIMAL;
    default:
        return LAMBDA_NUM_INVALID;
    }
}

static inline Type* lambda_numeric_type_from_kind(LambdaNumericKind kind) {
    switch (kind) {
    case LAMBDA_NUM_INT: return &TYPE_INT;
    case LAMBDA_NUM_INTEGER: return &TYPE_INTEGER_VALUE;
    case LAMBDA_NUM_FLOAT: return &TYPE_FLOAT;
    case LAMBDA_NUM_DECIMAL: return &TYPE_DECIMAL;
    case LAMBDA_NUM_I8: return &TYPE_I8;
    case LAMBDA_NUM_I16: return &TYPE_I16;
    case LAMBDA_NUM_I32: return &TYPE_I32;
    case LAMBDA_NUM_I64: return &TYPE_INT64;
    case LAMBDA_NUM_U8: return &TYPE_U8;
    case LAMBDA_NUM_U16: return &TYPE_U16;
    case LAMBDA_NUM_U32: return &TYPE_U32;
    case LAMBDA_NUM_U64: return &TYPE_UINT64;
    case LAMBDA_NUM_F16: return &TYPE_F16;
    case LAMBDA_NUM_F32: return &TYPE_F32;
    default: return &TYPE_ANY;
    }
}

#endif
