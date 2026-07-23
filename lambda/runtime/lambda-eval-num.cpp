
#include "transpiler.hpp"
#include "lambda-decimal.hpp"
#include "runtime/lambda-number-runtime.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/str.h"
#include "js/js_typed_array.h"
#include <stdarg.h>
#include <time.h>
#include <cstdlib>  // for abs function
#include <cmath>    // for pow function
#include <errno.h>  // for errno checking
#include <inttypes.h>  // for PRId64
#include <limits.h>

extern __thread EvalContext* context;

String* str_repeat(String* str, int64_t times);
String* fn_strcat(String* left, String* right);

// Helper functions for decimal operations
// push_decimal, convert_to_decimal, cleanup_temp_decimal are now centralized in lambda-decimal.cpp

Item push_c(int64_t cval) {
    if (cval == INT64_ERROR) { return ItemError; }
    return decimal_from_int64(cval, context);
}
// Use decimal_add/sub/mul/div/mod/pow from lambda-decimal.hpp instead.

// decimal_is_zero is now in lambda-decimal.cpp

// runtime lists and arrays share LMD_TYPE_ARRAY, so one array predicate covers both.
#define IS_VECTOR_TYPE(t) ((t) == LMD_TYPE_ARRAY_NUM || (t) == LMD_TYPE_ARRAY || (t) == LMD_TYPE_RANGE)

#define IS_SCALAR_NUMERIC(t) (is_integer_type_id((TypeId)(t)) || is_float_type_id((TypeId)(t)) || \
                              (t) == LMD_TYPE_DECIMAL || \
                              (t) == LMD_TYPE_NUM_SIZED || (t) == LMD_TYPE_UINT64)

struct SizedIntegerValue {
    bool is_unsigned;
    int bits;
    uint64_t raw;
    int64_t sval;
};

enum SizedIntegerOp {
    SIZED_INTEGER_ADD,
    SIZED_INTEGER_SUB,
    SIZED_INTEGER_MUL,
    SIZED_INTEGER_DIV,
    SIZED_INTEGER_MOD,
};

enum SizedFloatOp {
    SIZED_FLOAT_ADD,
    SIZED_FLOAT_SUB,
    SIZED_FLOAT_MUL,
    SIZED_FLOAT_DIV,
};

enum SizedBitwiseOp {
    SIZED_BITWISE_AND,
    SIZED_BITWISE_OR,
    SIZED_BITWISE_XOR,
};

static inline uint64_t sized_mask(int bits) {
    return bits >= 64 ? UINT64_MAX : ((1ULL << bits) - 1ULL);
}

static inline int64_t int64_from_bits(uint64_t raw) {
    int64_t result;
    __builtin_memcpy(&result, &raw, sizeof(result));
    return result;
}

static inline Item pack_compact_int_or_float(__int128 value) {
    // The Item payload can hold more bits than Lambda's semantic int; overflow
    // crosses into float instead of becoming a wider tagged int or an error.
    if (value <= INT56_MAX && value >= INT56_MIN) {
        return (Item){ .item = i2it((int64_t)value) };
    }
    return push_d((double)value);
}

static bool read_sized_integer(Item item, TypeId type, SizedIntegerValue* out) {
    if (type == LMD_TYPE_NUM_SIZED) {
        NumSizedType st = item.get_num_type();
        out->is_unsigned = false;
        switch (st) {
        case NUM_INT8:
            out->bits = 8;  out->sval = item.get_i8();   out->raw = (uint8_t)out->sval;  return true;
        case NUM_INT16:
            out->bits = 16; out->sval = item.get_i16();  out->raw = (uint16_t)out->sval; return true;
        case NUM_INT32:
            out->bits = 32; out->sval = item.get_i32();  out->raw = (uint32_t)out->sval; return true;
        case NUM_UINT8:
            out->bits = 8;  out->is_unsigned = true; out->raw = item.get_u8();  out->sval = (int64_t)out->raw; return true;
        case NUM_UINT16:
            out->bits = 16; out->is_unsigned = true; out->raw = item.get_u16(); out->sval = (int64_t)out->raw; return true;
        case NUM_UINT32:
            out->bits = 32; out->is_unsigned = true; out->raw = item.get_u32(); out->sval = (int64_t)out->raw; return true;
        default:
            return false;
        }
    }
    if (type == LMD_TYPE_INT64) {
        out->is_unsigned = false;
        out->bits = 64;
        out->sval = item.get_int64();
        out->raw = (uint64_t)out->sval;
        return true;
    }
    if (type == LMD_TYPE_UINT64) {
        out->is_unsigned = true;
        out->bits = 64;
        out->raw = item.get_uint64();
        out->sval = int64_from_bits(out->raw);
        return true;
    }
    return false;
}

static void sized_integer_result_type(const SizedIntegerValue* a, const SizedIntegerValue* b,
        bool* is_unsigned, int* bits) {
    LambdaNumericKind left = lambda_numeric_sized_kind(a->is_unsigned, a->bits);
    LambdaNumericKind right = lambda_numeric_sized_kind(b->is_unsigned, b->bits);
    LambdaNumericDecision decision = lambda_numeric_classify(
        LAMBDA_NUM_OP_ADD, left, right);
    // read_sized_integer guarantees the classifier's sized-lane precondition.
    *is_unsigned = lambda_numeric_sized_is_unsigned(decision.result);
    *bits = lambda_numeric_sized_bits(decision.result);
}

static inline int64_t sized_signed_operand(const SizedIntegerValue* value, int bits) {
    uint64_t raw = value->is_unsigned ? value->raw : (uint64_t)value->sval;
    if (bits < 64) {
        uint64_t sign_bit = 1ULL << (bits - 1);
        raw &= sized_mask(bits);
        if (raw & sign_bit) raw |= ~sized_mask(bits);
    }
    return int64_from_bits(raw);
}

static inline uint64_t sized_operand_raw(const SizedIntegerValue* value) {
    return value->is_unsigned ? value->raw : (uint64_t)value->sval;
}

static Item pack_sized_integer(bool is_unsigned, int bits, uint64_t raw) {
    raw &= sized_mask(bits);
    if (is_unsigned) {
        switch (bits) {
        case 8:  return (Item){ .item = u8_to_item(raw) };
        case 16: return (Item){ .item = u16_to_item(raw) };
        case 32: return (Item){ .item = u32_to_item(raw) };
        default: return box_uint64_value(raw);
        }
    }
    switch (bits) {
    case 8:  return (Item){ .item = i8_to_item(raw) };
    case 16: return (Item){ .item = i16_to_item(raw) };
    case 32: return (Item){ .item = i32_to_item(raw) };
    default: return box_int64_value(int64_from_bits(raw));
    }
}

static bool sized_integer_arithmetic(Item item_a, TypeId type_a, Item item_b, TypeId type_b,
        SizedIntegerOp op, Item* result) {
    SizedIntegerValue a, b;
    if (!read_sized_integer(item_a, type_a, &a) || !read_sized_integer(item_b, type_b, &b)) {
        return false;
    }

    bool is_unsigned = false;
    int bits = 0;
    sized_integer_result_type(&a, &b, &is_unsigned, &bits);

    uint64_t raw = 0;
    uint64_t mask = sized_mask(bits);
    uint64_t raw_a = sized_operand_raw(&a);
    uint64_t raw_b = sized_operand_raw(&b);
    if (op == SIZED_INTEGER_ADD) {
        raw = raw_a + raw_b;
    } else if (op == SIZED_INTEGER_SUB) {
        raw = raw_a - raw_b;
    } else if (op == SIZED_INTEGER_MUL) {
        raw = raw_a * raw_b;
    } else if (is_unsigned) {
        uint64_t divisor = raw_b & mask;
        if (divisor == 0) {
            log_error("sized integer division by zero error");
            *result = ItemError;
            return true;
        }
        raw = (op == SIZED_INTEGER_DIV) ? ((raw_a & mask) / divisor) : ((raw_a & mask) % divisor);
    } else {
        int64_t dividend = sized_signed_operand(&a, bits);
        int64_t divisor = sized_signed_operand(&b, bits);
        if (divisor == 0) {
            log_error("sized integer division by zero error");
            *result = ItemError;
            return true;
        }
        if (bits == 64 && dividend == INT64_MIN && divisor == -1) {
            raw = (op == SIZED_INTEGER_DIV) ? (uint64_t)INT64_MIN : 0;
        } else {
            raw = (op == SIZED_INTEGER_DIV) ? (uint64_t)(dividend / divisor) : (uint64_t)(dividend % divisor);
        }
    }
    *result = pack_sized_integer(is_unsigned, bits, raw);
    return true;
}

static bool sized_float_arithmetic(Item item_a, TypeId type_a, Item item_b, TypeId type_b,
        SizedFloatOp op, Item* result) {
    if (type_a != LMD_TYPE_NUM_SIZED || type_b != LMD_TYPE_NUM_SIZED) return false;
    NumSizedType st_a = item_a.get_num_type();
    NumSizedType st_b = item_b.get_num_type();
    bool a_float = (st_a == NUM_FLOAT16 || st_a == NUM_FLOAT32);
    bool b_float = (st_b == NUM_FLOAT16 || st_b == NUM_FLOAT32);
    if (!a_float || !b_float) return false;

    double a = item_a.get_num_sized_as_double();
    double b = item_b.get_num_sized_as_double();
    double value = 0.0;
    if (op == SIZED_FLOAT_ADD) value = a + b;
    else if (op == SIZED_FLOAT_SUB) value = a - b;
    else if (op == SIZED_FLOAT_MUL) value = a * b;
    else value = a / b;

    // Mixed sized-float lanes keep the smallest explicit lane that contains both operands.
    *result = (st_a == NUM_FLOAT32 || st_b == NUM_FLOAT32)
        ? (Item){ .item = f32_to_item((float)value) }
        : (Item){ .item = f16_to_item((float)value) };
    return true;
}

static int64_t runtime_integral_as_int64(Item item, LambdaNumericKind kind) {
    switch (kind) {
    case LAMBDA_NUM_INT: return item.get_int56();
    case LAMBDA_NUM_I8: return item.get_i8();
    case LAMBDA_NUM_I16: return item.get_i16();
    case LAMBDA_NUM_I32: return item.get_i32();
    case LAMBDA_NUM_I64: return item.get_int64();
    case LAMBDA_NUM_U8: return item.get_u8();
    case LAMBDA_NUM_U16: return item.get_u16();
    case LAMBDA_NUM_U32: return item.get_u32();
    default: return 0;
    }
}

static double runtime_numeric_as_double(Item item, LambdaNumericKind kind) {
    switch (kind) {
    case LAMBDA_NUM_FLOAT: return item.get_double();
    case LAMBDA_NUM_F16: case LAMBDA_NUM_F32:
        return item.get_num_sized_as_double();
    default:
        return (double)runtime_integral_as_int64(item, kind);
    }
}

static Item runtime_numeric_to_integer(Item item, LambdaNumericKind kind) {
    switch (kind) {
    case LAMBDA_NUM_INTEGER:
        return item;
    case LAMBDA_NUM_U64:
        return bigint_from_uint64(item.get_uint64());
    case LAMBDA_NUM_I64:
        return bigint_from_int64(item.get_int64());
    case LAMBDA_NUM_INT: case LAMBDA_NUM_I8: case LAMBDA_NUM_I16:
    case LAMBDA_NUM_I32: case LAMBDA_NUM_U8: case LAMBDA_NUM_U16:
    case LAMBDA_NUM_U32:
        return bigint_from_int64(runtime_integral_as_int64(item, kind));
    default:
        return ItemError;
    }
}

static Item runtime_numeric_decimal_operand(Item item, LambdaNumericKind kind) {
    // Full-width sized integers enter the semantic tower through integer even
    // when the final common domain is decimal.  This avoids any binary64 or
    // signed-cast detour and makes small and large u64 values take one route.
    if (kind == LAMBDA_NUM_I64 || kind == LAMBDA_NUM_U64 ||
        kind == LAMBDA_NUM_INTEGER) {
        return runtime_numeric_to_integer(item, kind);
    }
    return item;
}

static Item apply_decimal_numeric(Item item_a, LambdaNumericKind kind_a,
        Item item_b, LambdaNumericKind kind_b, LambdaNumericKind result_kind,
        LambdaNumericOpFamily op) {
    RootFrame roots((Context*)context, 2);
    Rooted<Item> rooted_a(roots, item_a);
    Rooted<Item> rooted_b(roots, item_b);

    Item converted_a = result_kind == LAMBDA_NUM_INTEGER ?
        runtime_numeric_to_integer(rooted_a.get(), kind_a) :
        runtime_numeric_decimal_operand(rooted_a.get(), kind_a);
    if (get_type_id(converted_a) == LMD_TYPE_ERROR) return ItemError;
    rooted_a.set(converted_a);

    Item converted_b = result_kind == LAMBDA_NUM_INTEGER ?
        runtime_numeric_to_integer(rooted_b.get(), kind_b) :
        runtime_numeric_decimal_operand(rooted_b.get(), kind_b);
    if (get_type_id(converted_b) == LMD_TYPE_ERROR) return ItemError;
    rooted_b.set(converted_b);

    switch (op) {
    case LAMBDA_NUM_OP_ADD: return decimal_add(rooted_a.get(), rooted_b.get(), context);
    case LAMBDA_NUM_OP_SUB: return decimal_sub(rooted_a.get(), rooted_b.get(), context);
    case LAMBDA_NUM_OP_MUL: return decimal_mul(rooted_a.get(), rooted_b.get(), context);
    case LAMBDA_NUM_OP_TRUE_DIV: return decimal_div(rooted_a.get(), rooted_b.get(), context);
    case LAMBDA_NUM_OP_IDIV: return decimal_idiv(rooted_a.get(), rooted_b.get(), context);
    case LAMBDA_NUM_OP_MOD: return decimal_mod(rooted_a.get(), rooted_b.get(), context);
    default: return ItemError;
    }
}

static bool apply_classified_numeric(Item item_a, TypeId type_a,
        Item item_b, TypeId type_b, LambdaNumericOpFamily op, Item* result) {
    bool int_a = type_a == LMD_TYPE_INT;
    bool int_b = type_b == LMD_TYPE_INT;
    bool float_a = is_float_type_id(type_a);
    bool float_b = is_float_type_id(type_b);

    bool common_a = int_a || float_a;
    bool common_b = int_b || float_b;
    if (common_a && common_b) {
        LambdaNumericKind common_kind_a = int_a ? LAMBDA_NUM_INT : LAMBDA_NUM_FLOAT;
        LambdaNumericKind common_kind_b = int_b ? LAMBDA_NUM_INT : LAMBDA_NUM_FLOAT;
        LambdaNumericDecision common = lambda_numeric_classify(
            op, common_kind_a, common_kind_b);
        if (!common.valid) return false;

        // These are the classifier's most frequent closed result cells. Avoid
        // generalized sized/decimal conversion after the classifier has fixed
        // their domain, or boxed numeric loops pay promotion overhead per item.
        if (common.result == LAMBDA_NUM_FLOAT) {
            double left = int_a ? (double)item_a.get_int56() : item_a.get_double();
            double right = int_b ? (double)item_b.get_int56() : item_b.get_double();
            double value;
            if (op == LAMBDA_NUM_OP_ADD) value = left + right;
            else if (op == LAMBDA_NUM_OP_SUB) value = left - right;
            else if (op == LAMBDA_NUM_OP_MUL) value = left * right;
            else if (op == LAMBDA_NUM_OP_TRUE_DIV) value = left / right;
            else return false;
            *result = push_d(value);
            return true;
        }
        if (common.result != LAMBDA_NUM_INT) return false;

        int64_t left = item_a.get_int56();
        int64_t right = item_b.get_int56();
        if (op == LAMBDA_NUM_OP_ADD) {
            *result = pack_compact_int_or_float((__int128)left + (__int128)right);
        } else if (op == LAMBDA_NUM_OP_SUB) {
            *result = pack_compact_int_or_float((__int128)left - (__int128)right);
        } else if (op == LAMBDA_NUM_OP_MUL) {
            *result = pack_compact_int_or_float((__int128)left * (__int128)right);
        } else if (op == LAMBDA_NUM_OP_TRUE_DIV) {
            *result = push_d((double)left / (double)right);
        } else if (right == 0 &&
                (op == LAMBDA_NUM_OP_IDIV || op == LAMBDA_NUM_OP_MOD)) {
            log_error("integer arithmetic zero divisor");
            *result = ItemError;
        } else if (op == LAMBDA_NUM_OP_IDIV) {
            *result = (Item){.item = i2it(left / right)};
        } else if (op == LAMBDA_NUM_OP_MOD) {
            *result = (Item){.item = i2it(left % right)};
        } else {
            return false;
        }
        return true;
    }

    LambdaNumericKind kind_a = lambda_numeric_kind_from_item_type(item_a, type_a);
    LambdaNumericKind kind_b = lambda_numeric_kind_from_item_type(item_b, type_b);
    if (kind_a == LAMBDA_NUM_INVALID || kind_b == LAMBDA_NUM_INVALID) return false;

    LambdaNumericDecision decision = lambda_numeric_classify(op, kind_a, kind_b);
    if (!decision.valid) return false;

    if (lambda_numeric_is_sized_integer(decision.result)) {
        SizedIntegerOp sized_op;
        if (op == LAMBDA_NUM_OP_ADD) sized_op = SIZED_INTEGER_ADD;
        else if (op == LAMBDA_NUM_OP_SUB) sized_op = SIZED_INTEGER_SUB;
        else if (op == LAMBDA_NUM_OP_MUL) sized_op = SIZED_INTEGER_MUL;
        else if (op == LAMBDA_NUM_OP_IDIV) sized_op = SIZED_INTEGER_DIV;
        else if (op == LAMBDA_NUM_OP_MOD) sized_op = SIZED_INTEGER_MOD;
        else return false;
        return sized_integer_arithmetic(item_a, type_a, item_b,
            type_b, sized_op, result);
    }

    if (lambda_numeric_is_sized_float(decision.result)) {
        SizedFloatOp sized_op = op == LAMBDA_NUM_OP_ADD ? SIZED_FLOAT_ADD :
            op == LAMBDA_NUM_OP_SUB ? SIZED_FLOAT_SUB :
            op == LAMBDA_NUM_OP_MUL ? SIZED_FLOAT_MUL : SIZED_FLOAT_DIV;
        return sized_float_arithmetic(item_a, type_a, item_b,
            type_b, sized_op, result);
    }

    if (decision.result == LAMBDA_NUM_INT) {
        int64_t left = runtime_integral_as_int64(item_a, kind_a);
        int64_t right = runtime_integral_as_int64(item_b, kind_b);
        if (op == LAMBDA_NUM_OP_ADD) {
            *result = pack_compact_int_or_float((__int128)left + (__int128)right);
        } else if (op == LAMBDA_NUM_OP_SUB) {
            *result = pack_compact_int_or_float((__int128)left - (__int128)right);
        } else if (op == LAMBDA_NUM_OP_MUL) {
            *result = pack_compact_int_or_float((__int128)left * (__int128)right);
        } else {
            if (right == 0) {
                log_error("integer arithmetic zero divisor");
                *result = ItemError;
            } else if (op == LAMBDA_NUM_OP_IDIV) {
                *result = (Item){.item = i2it(left / right)};
            } else if (op == LAMBDA_NUM_OP_MOD) {
                *result = (Item){.item = i2it(left % right)};
            } else {
                return false;
            }
        }
        return true;
    }

    if (decision.result == LAMBDA_NUM_FLOAT) {
        double left = runtime_numeric_as_double(item_a, kind_a);
        double right = runtime_numeric_as_double(item_b, kind_b);
        double value;
        if (op == LAMBDA_NUM_OP_ADD) value = left + right;
        else if (op == LAMBDA_NUM_OP_SUB) value = left - right;
        else if (op == LAMBDA_NUM_OP_MUL) value = left * right;
        else if (op == LAMBDA_NUM_OP_TRUE_DIV) value = left / right;
        else return false;
        *result = push_d(value);
        return true;
    }

    if (decision.result == LAMBDA_NUM_INTEGER ||
        decision.result == LAMBDA_NUM_DECIMAL) {
        *result = apply_decimal_numeric(item_a, kind_a, item_b, kind_b,
            decision.result, op);
        return true;
    }
    return false;
}

static bool sized_integer_neg(Item item, TypeId type, Item* result) {
    SizedIntegerValue value;
    if (!read_sized_integer(item, type, &value)) return false;
    *result = pack_sized_integer(value.is_unsigned, value.bits, 0ULL - value.raw);
    return true;
}

struct BitwiseIntegerValue {
    SizedIntegerValue value;
    bool is_sized;
};

static bool read_bitwise_integer(Item item, BitwiseIntegerValue* out) {
    TypeId type = get_type_id(item);
    if (read_sized_integer(item, type, &out->value)) {
        out->is_sized = true;
        return true;
    }
    out->is_sized = false;
    out->value.is_unsigned = false;
    out->value.bits = 64;
    switch (type) {
    case LMD_TYPE_INT:
        out->value.sval = item.get_int56();
        out->value.raw = (uint64_t)out->value.sval;
        return true;
    case LMD_TYPE_INT64:
        out->value.sval = item.get_int64();
        out->value.raw = (uint64_t)out->value.sval;
        return true;
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64:
        out->value.sval = (int64_t)item.get_double();
        out->value.raw = (uint64_t)out->value.sval;
        return true;
    case LMD_TYPE_BOOL:
        out->value.sval = item.bool_val ? 1 : 0;
        out->value.raw = (uint64_t)out->value.sval;
        return true;
    case LMD_TYPE_RAW_POINTER:
        out->value.raw = item.item;
        out->value.sval = int64_from_bits(out->value.raw);
        return true;
    case LMD_TYPE_NULL:
    case LMD_TYPE_ERROR:
        out->value.sval = 0;
        out->value.raw = 0;
        return true;
    default:
        out->value.sval = 0;
        out->value.raw = 0;
        return true;
    }
}

static bool sized_bitwise_binary(Item item_a, Item item_b, SizedBitwiseOp op, Item* result) {
    BitwiseIntegerValue a, b;
    if (!read_bitwise_integer(item_a, &a) || !read_bitwise_integer(item_b, &b)) return false;
    if (!a.is_sized && !b.is_sized) return false;

    bool is_unsigned = false;
    int bits = 0;
    if (a.is_sized && b.is_sized) {
        sized_integer_result_type(&a.value, &b.value, &is_unsigned, &bits);
    } else {
        SizedIntegerValue* sized_value = a.is_sized ? &a.value : &b.value;
        is_unsigned = sized_value->is_unsigned;
        bits = sized_value->bits;
    }

    uint64_t mask = sized_mask(bits);
    uint64_t raw_a = sized_operand_raw(&a.value) & mask;
    uint64_t raw_b = sized_operand_raw(&b.value) & mask;
    uint64_t raw = 0;
    if (op == SIZED_BITWISE_AND) raw = raw_a & raw_b;
    else if (op == SIZED_BITWISE_OR) raw = raw_a | raw_b;
    else raw = raw_a ^ raw_b;

    *result = pack_sized_integer(is_unsigned, bits, raw);
    return true;
}

static bool sized_bitwise_not(Item item, Item* result) {
    BitwiseIntegerValue value;
    if (!read_bitwise_integer(item, &value) || !value.is_sized) return false;
    uint64_t raw = ~sized_operand_raw(&value.value);
    *result = pack_sized_integer(value.value.is_unsigned, value.value.bits, raw);
    return true;
}

static bool sized_shift(Item item_a, Item item_b, bool shift_left, Item* result) {
    BitwiseIntegerValue a, b;
    if (!read_bitwise_integer(item_a, &a) || !a.is_sized) return false;
    if (!read_bitwise_integer(item_b, &b)) return false;

    int64_t count = b.value.sval;
    if (count < 0) {
        log_error("sized bitwise negative shift count");
        *result = ItemError;
        return true;
    }

    int bits = a.value.bits;
    uint64_t raw = 0;
    uint64_t mask = sized_mask(bits);
    if (shift_left) {
        raw = (count >= bits) ? 0 : ((sized_operand_raw(&a.value) & mask) << count);
    } else if (a.value.is_unsigned) {
        raw = (count >= bits) ? 0 : ((a.value.raw & mask) >> count);
    } else if (count >= bits) {
        raw = (sized_operand_raw(&a.value) & (1ULL << (bits - 1))) ? mask : 0;
    } else {
        int64_t signed_value = sized_signed_operand(&a.value, bits);
        raw = (uint64_t)(signed_value >> count);
    }

    *result = pack_sized_integer(a.value.is_unsigned, bits, raw);
    return true;
}

static bool apply_classified_bitwise(Item item_a, Item item_b,
        SizedBitwiseOp op, Item* result) {
    LambdaNumericKind kind_a = lambda_numeric_kind_from_item(item_a);
    LambdaNumericKind kind_b = lambda_numeric_kind_from_item(item_b);
    LambdaNumericDecision decision = lambda_numeric_classify(
        LAMBDA_NUM_OP_BITWISE, kind_a, kind_b);
    if (!decision.valid) return false;

    if (lambda_numeric_is_sized_integer(decision.result)) {
        return sized_bitwise_binary(item_a, item_b, op, result);
    }
    if (decision.result == LAMBDA_NUM_INT) {
        int64_t left = runtime_integral_as_int64(item_a, kind_a);
        int64_t right = runtime_integral_as_int64(item_b, kind_b);
        uint64_t raw = op == SIZED_BITWISE_AND ? (uint64_t)left & (uint64_t)right :
            op == SIZED_BITWISE_OR ? (uint64_t)left | (uint64_t)right :
                                     (uint64_t)left ^ (uint64_t)right;
        *result = pack_compact_int_or_float((__int128)int64_from_bits(raw));
        return true;
    }
    if (decision.result == LAMBDA_NUM_INTEGER) {
        RootFrame roots((Context*)context, 2);
        Rooted<Item> left(roots, item_a);
        Rooted<Item> right(roots, item_b);
        left.set(runtime_numeric_to_integer(left.get(), kind_a));
        if (get_type_id(left.get()) == LMD_TYPE_ERROR) {
            *result = ItemError;
            return true;
        }
        right.set(runtime_numeric_to_integer(right.get(), kind_b));
        if (get_type_id(right.get()) == LMD_TYPE_ERROR) {
            *result = ItemError;
            return true;
        }
        *result = op == SIZED_BITWISE_AND ? bigint_bitwise_and(left.get(), right.get()) :
            op == SIZED_BITWISE_OR ? bigint_bitwise_or(left.get(), right.get()) :
                                    bigint_bitwise_xor(left.get(), right.get());
        return true;
    }
    return false;
}

static bool apply_classified_shift(Item item_a, Item item_b,
        bool shift_left, Item* result) {
    LambdaNumericKind kind_a = lambda_numeric_kind_from_item(item_a);
    LambdaNumericKind kind_b = lambda_numeric_kind_from_item(item_b);
    LambdaNumericDecision decision = lambda_numeric_classify(
        LAMBDA_NUM_OP_SHIFT, kind_a, kind_b);
    if (!decision.valid) return false;

    if (lambda_numeric_is_sized_integer(decision.result)) {
        return sized_shift(item_a, item_b, shift_left, result);
    }
    if (decision.result == LAMBDA_NUM_INT) {
        int64_t left = runtime_integral_as_int64(item_a, kind_a);
        int64_t count = runtime_integral_as_int64(item_b, kind_b);
        if (count < 0) {
            log_error("integer negative shift count");
            *result = ItemError;
            return true;
        }
        uint64_t raw = count >= 64 ? 0 : shift_left ?
            (uint64_t)left << count : (uint64_t)(left >> count);
        *result = pack_compact_int_or_float((__int128)int64_from_bits(raw));
        return true;
    }
    if (decision.result == LAMBDA_NUM_INTEGER) {
        RootFrame roots((Context*)context, 2);
        Rooted<Item> left(roots, item_a);
        Rooted<Item> right(roots, item_b);
        left.set(runtime_numeric_to_integer(left.get(), kind_a));
        if (get_type_id(left.get()) == LMD_TYPE_ERROR) {
            *result = ItemError;
            return true;
        }
        right.set(runtime_numeric_to_integer(right.get(), kind_b));
        if (get_type_id(right.get()) == LMD_TYPE_ERROR) {
            *result = ItemError;
            return true;
        }
        *result = shift_left ? bigint_left_shift(left.get(), right.get()) :
                               bigint_right_shift(left.get(), right.get());
        return true;
    }
    return false;
}

// forward declarations for vector ops (defined in lambda-vector.cpp)
Item vec_add(Item a, Item b);
Item vec_sub(Item a, Item b);
Item vec_mul(Item a, Item b);
Item vec_div(Item a, Item b);
Item vec_idiv(Item a, Item b);
Item vec_mod(Item a, Item b);
Item vec_pow(Item a, Item b);

// helper: get length of a vector-like item
static int64_t vector_length(Item item) {
    TypeId type = get_type_id(item);
    switch (type) {
        case LMD_TYPE_ARRAY_NUM:   return item.array_num->length;
        case LMD_TYPE_ARRAY:        return item.array->length;
        case LMD_TYPE_RANGE:       return item.range->length;
        default:                   return -1;
    }
}

// helper: get element from a vector-like item at index
static Item vector_get(Item item, int64_t index) {
    TypeId type = get_type_id(item);
    switch (type) {
        case LMD_TYPE_ARRAY_NUM:
            return array_num_read_item(item.array_num, index);
        case LMD_TYPE_ARRAY:
            return item.array->items[index];
        case LMD_TYPE_RANGE:
            return { .item = i2it(item.range->start + index) };
        default:
            return ItemError;
    }
}

enum NumericVectorUnaryOp {
    NUMERIC_VECTOR_ABS,
    NUMERIC_VECTOR_ROUND,
    NUMERIC_VECTOR_FLOOR,
    NUMERIC_VECTOR_CEIL,
};

static Item numeric_vector_unary_float(Item item, NumericVectorUnaryOp op,
        const char* function_name) {
    int64_t length = vector_length(item);
    if (length < 0) return ItemError;

    RootFrame roots((Context*)context, 2);
    Rooted<Item> rooted_source(roots, item);
    Rooted<ArrayNum*> rooted_result(roots, array_float_new(length));
    for (int64_t i = 0; i < length; i++) {
        Item element = vector_get(rooted_source.get(), i);
        TypeId element_type = get_type_id(element);
        if (!is_numeric_type_id(element_type)) {
            log_error("%s: non-numeric element at index %ld, type: %d",
                function_name, i, element_type);
            return ItemError;
        }
        double value = it2d(element);
        switch (op) {
        case NUMERIC_VECTOR_ABS: value = fabs(value); break;
        case NUMERIC_VECTOR_ROUND: value = round(value); break;
        case NUMERIC_VECTOR_FLOOR: value = floor(value); break;
        case NUMERIC_VECTOR_CEIL: value = ceil(value); break;
        }
        rooted_result.get()->float_items[i] = value;
    }
    return {.array_num = rooted_result.get()};
}

Item fn_numeric_fold(Item item, int multiply, int skip_null, int64_t* count_out) {
    GUARD_ERROR1(item);
    int64_t length = vector_length(item);
    if (length < 0) {
        if (count_out) *count_out = IS_NUMERIC_ID(get_type_id(item)) ? 1 : 0;
        return IS_NUMERIC_ID(get_type_id(item)) ? item : ItemError;
    }

    RootFrame roots((Context*)context, 2);
    Rooted<Item> rooted_source(roots, item);
    Rooted<Item> accumulator(roots, ItemNull);
    int64_t count = 0;
    for (int64_t i = 0; i < length; i++) {
        Item element = vector_get(rooted_source.get(), i);
        TypeId element_type = get_type_id(element);
        if (element_type == LMD_TYPE_NULL) {
            if (skip_null) continue;
            if (count_out) *count_out = count;
            return ItemNull;
        }
        if (!multiply && element_type == LMD_TYPE_BOOL) {
            // Boolean masks are numeric counts for sum(); retain the established
            // mask-reduction contract while the fold shares scalar arithmetic.
            element = (Item){.item = i2it(element.bool_val ? 1 : 0)};
            element_type = LMD_TYPE_INT;
        }
        if (!IS_NUMERIC_ID(element_type)) {
            log_error("numeric fold: non-numeric element at index %ld", i);
            return ItemError;
        }
        if (count == 0) {
            accumulator.set(element);
        } else {
            Item next = multiply ? fn_mul(accumulator.get(), element) :
                                   fn_add(accumulator.get(), element);
            if (get_type_id(next) == LMD_TYPE_ERROR) return ItemError;
            accumulator.set(next);
        }
        count++;
    }
    if (count_out) *count_out = count;
    return count == 0 ? (skip_null ? ItemNull :
        (multiply ? (Item){.item = i2it(1)} : (Item){.item = i2it(0)})) :
        accumulator.get();
}

Item fn_add(Item item_a, Item item_b) {
    GUARD_ERROR2(item_a, item_b);
    TypeId type_a = get_type_id(item_a);  TypeId type_b = get_type_id(item_b);

    // null propagation: null + x = null
    if (type_a == LMD_TYPE_NULL || type_b == LMD_TYPE_NULL) return ItemNull;

    // vector operations: scalar+vector, vector+scalar, or vector+vector
    if ((IS_SCALAR_NUMERIC(type_a) && IS_VECTOR_TYPE(type_b)) ||
        (IS_VECTOR_TYPE(type_a) && IS_SCALAR_NUMERIC(type_b)) ||
        (IS_VECTOR_TYPE(type_a) && IS_VECTOR_TYPE(type_b))) {
        return vec_add(item_a, item_b);
    }
    Item result;
    if (apply_classified_numeric(item_a, type_a, item_b, type_b,
            LAMBDA_NUM_OP_ADD, &result)) return result;
    log_error("unknown add type: %d, %d", type_a, type_b);
    return ItemError;
}

Item fn_mul(Item item_a, Item item_b) {
    GUARD_ERROR2(item_a, item_b);
    TypeId type_a = get_type_id(item_a);  TypeId type_b = get_type_id(item_b);

    // null propagation: null * x = null
    if (type_a == LMD_TYPE_NULL || type_b == LMD_TYPE_NULL) return ItemNull;

    // vector operations
    if ((IS_SCALAR_NUMERIC(type_a) && IS_VECTOR_TYPE(type_b)) ||
        (IS_VECTOR_TYPE(type_a) && IS_SCALAR_NUMERIC(type_b)) ||
        (IS_VECTOR_TYPE(type_a) && IS_VECTOR_TYPE(type_b))) {
        return vec_mul(item_a, item_b);
    }
    Item result;
    if (apply_classified_numeric(item_a, type_a, item_b, type_b,
            LAMBDA_NUM_OP_MUL, &result)) return result;
    log_error("unknown mul type: %d, %d", type_a, type_b);
    return ItemError;
}

Item fn_sub(Item item_a, Item item_b) {
    GUARD_ERROR2(item_a, item_b);
    TypeId type_a = get_type_id(item_a);  TypeId type_b = get_type_id(item_b);

    // null propagation: null - x = null
    if (type_a == LMD_TYPE_NULL || type_b == LMD_TYPE_NULL) return ItemNull;

    // vector operations
    if ((IS_SCALAR_NUMERIC(type_a) && IS_VECTOR_TYPE(type_b)) ||
        (IS_VECTOR_TYPE(type_a) && IS_SCALAR_NUMERIC(type_b)) ||
        (IS_VECTOR_TYPE(type_a) && IS_VECTOR_TYPE(type_b))) {
        return vec_sub(item_a, item_b);
    }
    Item result;
    if (apply_classified_numeric(item_a, type_a, item_b, type_b,
            LAMBDA_NUM_OP_SUB, &result)) return result;
    log_error("unknown sub type: %d, %d", type_a, type_b);
    return ItemError;
}

Item fn_div(Item item_a, Item item_b) {
    GUARD_ERROR2(item_a, item_b);
    TypeId type_a = get_type_id(item_a);  TypeId type_b = get_type_id(item_b);

    // null propagation: null / x = null, x / null = null
    if (type_a == LMD_TYPE_NULL || type_b == LMD_TYPE_NULL) return ItemNull;

    // vector operations
    if ((IS_SCALAR_NUMERIC(type_a) && IS_VECTOR_TYPE(type_b)) ||
        (IS_VECTOR_TYPE(type_a) && IS_SCALAR_NUMERIC(type_b)) ||
        (IS_VECTOR_TYPE(type_a) && IS_VECTOR_TYPE(type_b))) {
        return vec_div(item_a, item_b);
    }
    Item result;
    if (apply_classified_numeric(item_a, type_a, item_b, type_b,
            LAMBDA_NUM_OP_TRUE_DIV, &result)) return result;
    log_error("unknown div type: %d, %d", type_a, type_b);
    return ItemError;
}

Item fn_idiv(Item item_a, Item item_b) {
    GUARD_ERROR2(item_a, item_b);

    // null propagation: null // x = null
    TypeId ta = get_type_id(item_a), tb = get_type_id(item_b);
    if (ta == LMD_TYPE_NULL || tb == LMD_TYPE_NULL) return ItemNull;

    if ((IS_SCALAR_NUMERIC(ta) && IS_VECTOR_TYPE(tb)) ||
        (IS_VECTOR_TYPE(ta) && IS_SCALAR_NUMERIC(tb)) ||
        (IS_VECTOR_TYPE(ta) && IS_VECTOR_TYPE(tb))) {
        return vec_idiv(item_a, item_b);
    }

    Item result;
    if (apply_classified_numeric(item_a, ta, item_b, tb,
            LAMBDA_NUM_OP_IDIV, &result)) return result;
    log_error("unknown idiv type: %d, %d", ta, tb);
    return ItemError;
}

Item fn_pow(Item item_a, Item item_b) {
    GUARD_ERROR2(item_a, item_b);
    // Defensive check: verify items are valid before accessing
    // Check if the item union pointer fields are valid (not small integers that indicate corruption)
    uint64_t ptr_a = item_a.item;
    uint64_t ptr_b = item_b.item;
    if (ptr_a != 0 && ptr_a < 0x1000) {
        log_error("fn_pow: item_a has invalid/corrupt pointer value: 0x%llx", (unsigned long long)ptr_a);
        return ItemError;
    }
    if (ptr_b != 0 && ptr_b < 0x1000) {
        log_error("fn_pow: item_b has invalid/corrupt pointer value: 0x%llx", (unsigned long long)ptr_b);
        return ItemError;
    }

    TypeId type_a = get_type_id(item_a);  TypeId type_b = get_type_id(item_b);

    // vector operations
    if ((IS_SCALAR_NUMERIC(type_a) && IS_VECTOR_TYPE(type_b)) ||
        (IS_VECTOR_TYPE(type_a) && IS_SCALAR_NUMERIC(type_b)) ||
        (IS_VECTOR_TYPE(type_a) && IS_VECTOR_TYPE(type_b))) {
        return vec_pow(item_a, item_b);
    }

    LambdaNumericKind kind_a = lambda_numeric_kind_from_item(item_a);
    LambdaNumericKind kind_b = lambda_numeric_kind_from_item(item_b);
    if (kind_a == LAMBDA_NUM_INVALID || kind_b == LAMBDA_NUM_INVALID) {
        log_error("unknown pow types: %d, %d", type_a, type_b);
        return ItemError;
    }

    bool decimal_domain = kind_a == LAMBDA_NUM_INTEGER || kind_b == LAMBDA_NUM_INTEGER ||
        kind_a == LAMBDA_NUM_DECIMAL || kind_b == LAMBDA_NUM_DECIMAL ||
        kind_a == LAMBDA_NUM_I64 || kind_b == LAMBDA_NUM_I64 ||
        kind_a == LAMBDA_NUM_U64 || kind_b == LAMBDA_NUM_U64;
    if (decimal_domain) {
        RootFrame roots((Context*)context, 2);
        Rooted<Item> rooted_a(roots, item_a);
        Rooted<Item> rooted_b(roots, item_b);
        rooted_a.set(runtime_numeric_decimal_operand(rooted_a.get(), kind_a));
        if (get_type_id(rooted_a.get()) == LMD_TYPE_ERROR) return ItemError;
        rooted_b.set(runtime_numeric_decimal_operand(rooted_b.get(), kind_b));
        if (get_type_id(rooted_b.get()) == LMD_TYPE_ERROR) return ItemError;
        return decimal_pow(rooted_a.get(), rooted_b.get(), context);
    }

    return push_d(pow(runtime_numeric_as_double(item_a, kind_a),
                      runtime_numeric_as_double(item_b, kind_b)));
}

Item fn_mod(Item item_a, Item item_b) {
    GUARD_ERROR2(item_a, item_b);
    TypeId type_a = get_type_id(item_a);  TypeId type_b = get_type_id(item_b);

    // null propagation: null % x = null
    if (type_a == LMD_TYPE_NULL || type_b == LMD_TYPE_NULL) return ItemNull;

    // vector operations
    if ((IS_SCALAR_NUMERIC(type_a) && IS_VECTOR_TYPE(type_b)) ||
        (IS_VECTOR_TYPE(type_a) && IS_SCALAR_NUMERIC(type_b)) ||
        (IS_VECTOR_TYPE(type_a) && IS_VECTOR_TYPE(type_b))) {
        return vec_mod(item_a, item_b);
    }
    Item result;
    if (apply_classified_numeric(item_a, type_a, item_b, type_b,
            LAMBDA_NUM_OP_MOD, &result)) return result;
    log_error("unknown mod type: %d, %d", type_a, type_b);
    return ItemError;
}

// Numeric system functions implementation

Item fn_abs(Item item) {
    GUARD_ERROR1(item);
    // abs() - absolute value of a number or element-wise for arrays
    TypeId type = get_type_id(item);
    LambdaNumericKind kind = lambda_numeric_kind_from_item(item);
    if (lambda_numeric_is_sized_integer(kind)) {
        if (lambda_numeric_sized_is_unsigned(kind)) return item;
        int64_t value = runtime_integral_as_int64(item, kind);
        if (value >= 0) return item;
        Item result;
        return sized_integer_neg(item, type, &result) ? result : ItemError;
    }
    if (lambda_numeric_is_sized_float(kind)) {
        double value = fabs(item.get_num_sized_as_double());
        return kind == LAMBDA_NUM_F32 ?
            (Item){.item = f32_to_item((float)value)} :
            (Item){.item = f16_to_item((float)value)};
    }
    if (type == LMD_TYPE_INT) {
        int64_t val = item.get_int56();
        return (Item) { .item = i2it(val < 0 ? -val : val) };
    }
    else if (type == LMD_TYPE_INT64) {
        int64_t val = item.get_int64();
        return box_int64_value(val < 0 ? -val : val);
    }
    else if (type == LMD_TYPE_FLOAT) {
        double val = item.get_double();
        return push_d(fabs(val));
    }
    else if (IS_VECTOR_TYPE(type)) {
        return numeric_vector_unary_float(item, NUMERIC_VECTOR_ABS, "abs");
    }
    else if (type == LMD_TYPE_DECIMAL) {
        return decimal_abs(item, context);
    }
    else {
        log_error("abs not supported for type: %d", type);
        return ItemError;
    }
}

Item fn_round(Item item) {
    GUARD_ERROR1(item);
    // round() - round to nearest integer, or element-wise for arrays
    TypeId type = get_type_id(item);
    LambdaNumericKind kind = lambda_numeric_kind_from_item(item);
    if (kind == LAMBDA_NUM_INT || kind == LAMBDA_NUM_INTEGER ||
        lambda_numeric_is_sized_integer(kind)) {
        // Already an integer, return as-is
        return item;
    }
    else if (lambda_numeric_is_sized_float(kind)) {
        return push_d(round(item.get_num_sized_as_double()));
    }
    else if (type == LMD_TYPE_FLOAT) {
        double val = item.get_double();
        return push_d(round(val));
    }
    else if (IS_VECTOR_TYPE(type)) {
        return numeric_vector_unary_float(item, NUMERIC_VECTOR_ROUND, "round");
    }
    else if (type == LMD_TYPE_DECIMAL) {
        return decimal_round(item, context);
    }
    else {
        log_debug("round not supported for type: %d", type);
        return ItemError;
    }
}

Item fn_floor(Item item) {
    GUARD_ERROR1(item);
    // floor() - round down to nearest integer, or element-wise for arrays
    TypeId type = get_type_id(item);
    LambdaNumericKind kind = lambda_numeric_kind_from_item(item);
    if (kind == LAMBDA_NUM_INT || kind == LAMBDA_NUM_INTEGER ||
        lambda_numeric_is_sized_integer(kind)) {
        return item;  // return as-is
    }
    else if (lambda_numeric_is_sized_float(kind)) {
        return push_d(floor(item.get_num_sized_as_double()));
    }
    else if (type == LMD_TYPE_FLOAT) {
        double val = item.get_double();
        return push_d(floor(val));
    }
    else if (IS_VECTOR_TYPE(type)) {
        return numeric_vector_unary_float(item, NUMERIC_VECTOR_FLOOR, "floor");
    }
    else if (type == LMD_TYPE_DECIMAL) {
        return decimal_floor(item, context);
    }
    else {
        log_debug("floor not supported for type: %d", type);
        return ItemError;
    }
}

Item fn_ceil(Item item) {
    GUARD_ERROR1(item);
    // ceil() - round up to nearest integer, or element-wise for arrays
    TypeId type = get_type_id(item);
    LambdaNumericKind kind = lambda_numeric_kind_from_item(item);
    if (kind == LAMBDA_NUM_INT || kind == LAMBDA_NUM_INTEGER ||
        lambda_numeric_is_sized_integer(kind)) {
        return item;  // return as-is
    }
    else if (lambda_numeric_is_sized_float(kind)) {
        return push_d(ceil(item.get_num_sized_as_double()));
    }
    else if (type == LMD_TYPE_FLOAT) {
        double val = item.get_double();
        return push_d(ceil(val));
    }
    else if (IS_VECTOR_TYPE(type)) {
        return numeric_vector_unary_float(item, NUMERIC_VECTOR_CEIL, "ceil");
    }
    else if (type == LMD_TYPE_DECIMAL) {
        return decimal_ceil(item, context);
    }
    else {
        log_debug("ceil not supported for type: %d", type);
        return ItemError;
    }
}

static Item numeric_extreme_pair(Item left, Item right, bool minimum, bool* valid) {
    LambdaNumericComparison comparison = lambda_numeric_compare(left, right);
    if (!comparison.valid) {
        if (valid) *valid = false;
        return ItemError;
    }
    if (valid) *valid = true;

    LambdaNumericRuntimePart left_part;
    LambdaNumericRuntimePart right_part;
    bool left_simple = lambda_numeric_runtime_part(left, &left_part);
    bool right_simple = lambda_numeric_runtime_part(right, &right_part);
    bool left_float = left_simple && left_part.kind == LAMBDA_NUM_PART_FLOAT;
    bool right_float = right_simple && right_part.kind == LAMBDA_NUM_PART_FLOAT;
    if (comparison.unordered) {
        // Preserve the originating NaN and its sized-float payload when possible.
        return left_float && isnan(left_part.float_value) ? left : right;
    }

    if (comparison.order == 0 && (left_float || right_float)) {
        double left_value = left_float ? left_part.float_value : 0.0;
        double right_value = right_float ? right_part.float_value : 0.0;
        if (left_value == 0.0 && right_value == 0.0) {
            bool negative = minimum ? signbit(left_value) || signbit(right_value) :
                                      signbit(left_value) && signbit(right_value);
            if (left_float && signbit(left_value) == negative) return left;
            if (right_float && signbit(right_value) == negative) return right;
            // Mixed integer zero and -0 need a synthesized +0 for max.
            return push_d(negative ? -0.0 : 0.0);
        }
    }

    return minimum ? (comparison.order <= 0 ? left : right) :
                     (comparison.order >= 0 ? left : right);
}

static Item numeric_extreme_collection(Item item, bool minimum) {
    int64_t length = vector_length(item);
    if (length == 0) return ItemNull;
    if (length < 0) return IS_NUMERIC_ID(get_type_id(item)) ? item : ItemError;

    RootFrame roots((Context*)context, 2);
    Rooted<Item> rooted_source(roots, item);
    Rooted<Item> selected(roots, vector_get(item, 0));
    if (get_type_id(selected.get()) == LMD_TYPE_NULL) return ItemNull;
    if (!IS_NUMERIC_ID(get_type_id(selected.get()))) return ItemError;

    for (int64_t i = 1; i < length; i++) {
        Item element = vector_get(rooted_source.get(), i);
        if (get_type_id(element) == LMD_TYPE_NULL) return ItemNull;
        bool valid = false;
        Item next = numeric_extreme_pair(selected.get(), element, minimum, &valid);
        if (!valid) return ItemError;
        selected.set(next);
    }
    return selected.get();
}

Item fn_min2(Item item_a, Item item_b) {
    GUARD_ERROR2(item_a, item_b);
    // ArrayNum in the first slot is the established min(arr, axis) overload.
    if (get_type_id(item_a) == LMD_TYPE_ARRAY_NUM) return fn_min_axis(item_a, item_b);
    bool valid = false;
    Item result = numeric_extreme_pair(item_a, item_b, true, &valid);
    if (!valid) log_debug("min not supported for types: %d, %d",
        get_type_id(item_a), get_type_id(item_b));
    return valid ? result : ItemError;
}

Item fn_min1(Item item_a) {
    GUARD_ERROR1(item_a);
    if (is_text_type_id(get_type_id(item_a))) return item_a;
    return numeric_extreme_collection(item_a, true);
}

Item fn_max2(Item item_a, Item item_b) {
    GUARD_ERROR2(item_a, item_b);
    if (get_type_id(item_a) == LMD_TYPE_ARRAY_NUM) return fn_max_axis(item_a, item_b);
    bool valid = false;
    Item result = numeric_extreme_pair(item_a, item_b, false, &valid);
    if (!valid) log_debug("max not supported for types: %d, %d",
        get_type_id(item_a), get_type_id(item_b));
    return valid ? result : ItemError;
}

Item fn_max1(Item item_a) {
    GUARD_ERROR1(item_a);
    if (is_text_type_id(get_type_id(item_a))) return item_a;
    return numeric_extreme_collection(item_a, false);
}

Item fn_sum(Item item) {
    int64_t count = 0;
    return fn_numeric_fold(item, 0, 0, &count);
}

Item fn_avg_skip_null(Item item, bool skip_null) {
    int64_t count = 0;
    Item sum = fn_numeric_fold(item, 0, skip_null ? 1 : 0, &count);
    if (get_type_id(sum) == LMD_TYPE_ERROR || get_type_id(sum) == LMD_TYPE_NULL) return sum;
    // Average is the scalar fold followed by the model's true division. Full
    // sized lanes therefore enter decimal, while compact/int lanes enter float.
    return count == 0 ? ItemNull : fn_div(sum, (Item){.item = i2it(count)});
}

Item fn_avg(Item item) {
    return fn_avg_skip_null(item, false);
}

Item fn_pos(Item item) {
    GUARD_ERROR1(item);
    // Unary + operator - return the item as-is for numeric types, or cast strings/symbols to numbers
    if (get_type_id(item) == LMD_TYPE_INT) {
        return item;  // Already in correct format
    }
    else if (get_type_id(item) == LMD_TYPE_INT64) {
        return item;  // Already in correct format
    }
    else if (get_type_id(item) == LMD_TYPE_FLOAT) {
        return item;  // Already in correct format
    }
    else if (get_type_id(item) == LMD_TYPE_DECIMAL) {
        return item;  // For decimal, unary + returns the same value
    }
    else if (get_type_id(item) == LMD_TYPE_NUM_SIZED || get_type_id(item) == LMD_TYPE_UINT64) {
        return item;  // Already numeric
    }
    else if (is_text_type_id(get_type_id(item))) {
        // Cast string/symbol to number
        const char* chars = item.get_chars();
        uint32_t len = item.get_len();
        if (!chars || len == 0) {
            log_error("unary + error: empty string/symbol");
            return ItemError;
        }

        // Try to parse as integer first
        char* endptr;
        int64_t long_val = strtol(chars, &endptr, 10);

        // If entire string was consumed and no overflow, it's an integer
        if (*endptr == '\0' && endptr == chars + len) {
            return (Item) { .item = i2it(long_val) };
        }

        // Try to parse as float
        double double_val = strtod(chars, &endptr);
        if (*endptr == '\0' && endptr == chars + len) {
            return push_d(double_val);
        }

        // Not a valid number
        log_error("unary + error: cannot convert '%.*s' to number", (int)len, chars);
        return ItemError;
    }
    else {
        // For other types (bool, datetime, etc.), unary + is an error
        log_debug("unary + not supported for type: %d", get_type_id(item));
        return ItemError;
    }
}

Item fn_neg(Item item) {
    GUARD_ERROR1(item);
    // Unary - operator - negate numeric values or cast and negate strings/symbols
    if (get_type_id(item) == LMD_TYPE_INT) {
        int64_t val = item.get_int56();
        return (Item) { .item = i2it(-val) };
    }
    else if (get_type_id(item) == LMD_TYPE_INT64) {
        Item sized_result;
        if (sized_integer_neg(item, LMD_TYPE_INT64, &sized_result)) return sized_result;
        return ItemError;
    }
    else if (get_type_id(item) == LMD_TYPE_FLOAT) {
        double val = item.get_double();
        return push_d(-val);
    }
    else if (get_type_id(item) == LMD_TYPE_DECIMAL) {
        return decimal_neg(item, context);
    }
    else if (get_type_id(item) == LMD_TYPE_NUM_SIZED) {
        NumSizedType st = item.get_num_type();
        if (st == NUM_FLOAT16 || st == NUM_FLOAT32) {
            return push_d(-item.get_num_sized_as_double());
        } else {
            Item sized_result;
            if (sized_integer_neg(item, LMD_TYPE_NUM_SIZED, &sized_result)) return sized_result;
            return ItemError;
        }
    }
    else if (get_type_id(item) == LMD_TYPE_UINT64) {
        Item sized_result;
        if (sized_integer_neg(item, LMD_TYPE_UINT64, &sized_result)) return sized_result;
        return ItemError;
    }
    else if (get_type_id(item) == LMD_TYPE_ARRAY_NUM ||
             get_type_id(item) == LMD_TYPE_ARRAY ||
             get_type_id(item) == LMD_TYPE_RANGE) {
        TypeId t = get_type_id(item);
        int64_t len = (t == LMD_TYPE_ARRAY_NUM) ? item.array_num->length :
                      (t == LMD_TYPE_ARRAY) ? item.array->length :
                      item.range->length;
        if (len == 0) {
            ArrayNum* result = array_float_new(0);
            return { .array_num = result };
        }
        ArrayNum* result = array_float_new(len);
        for (int64_t i = 0; i < len; i++) {
            Item elem;
            if (t == LMD_TYPE_ARRAY_NUM) {
                ArrayNum* arr = item.array_num;
                ArrayNumElemType et = arr->get_elem_type();
                if (et == ELEM_FLOAT64) { result->float_items[i] = -arr->float_items[i]; continue; }
                if (et == ELEM_INT || et == ELEM_INT64) {
                    result->float_items[i] = -(double)arr->items[i]; continue;
                }
                // compact elements: fall through to generic via array_num_get
                elem = array_num_get(arr, i);
            } else if (t == LMD_TYPE_ARRAY) {
                elem = item.array->items[i];
            } else {
                elem = (Item){ .item = i2it(item.range->start + i) };
            }
            TypeId et = get_type_id(elem);
            if (et == LMD_TYPE_INT) {
                result->float_items[i] = -(double)elem.get_int56();
            } else if (et == LMD_TYPE_INT64) {
                result->float_items[i] = -(double)elem.get_int64();
            } else if (et == LMD_TYPE_UINT64) {
                // u64 uses a distinct pointer-backed tag, so omitting it here
                // rejected a numeric lane that the scalar path already accepts.
                result->float_items[i] = -(double)elem.get_uint64();
            } else if (et == LMD_TYPE_NUM_SIZED) {
                result->float_items[i] = -elem.get_num_sized_as_double();
            } else if (et == LMD_TYPE_FLOAT) {
                result->float_items[i] = -elem.get_double();
            } else {
                log_error("neg: non-numeric element at index %ld, type: %d", i, et);
                return ItemError;
            }
        }
        return { .array_num = result };
    }
    else if (is_text_type_id(get_type_id(item))) {
        // Cast string/symbol to number, then negate
        const char* chars = item.get_chars();
        uint32_t len = item.get_len();
        if (!chars || len == 0) {
            log_debug("unary - error: empty string/symbol");
            return ItemError;
        }

        // Try to parse as integer first
        char* endptr;
        int64_t long_val = strtol(chars, &endptr, 10);

        // If entire string was consumed and no overflow, it's an integer
        if (*endptr == '\0' && endptr == chars + len) {
            return (Item) { .item = i2it(-long_val) };
        }

        // Try to parse as float
        double double_val = strtod(chars, &endptr);
        if (*endptr == '\0' && endptr == chars + len) {
            return push_d(-double_val);
        }

        // Not a valid number
        log_debug("unary - error: cannot convert '%.*s' to number", (int)len, chars);
        return ItemError;
    }
    else {
        // For other types (bool, datetime, etc.), unary - is an error
        log_debug("unary - not supported for type: %d", get_type_id(item));
        return ItemError;
    }
}

Item fn_int(Item item) {
    GUARD_ERROR1(item);
    double dval;  int64_t ival;
    if (get_type_id(item) == LMD_TYPE_INT) {
        return item;
    }
    else if (get_type_id(item) == LMD_TYPE_INT64) {
        dval = item.get_int64();
        goto CHECK_DVAL;
    }
    else if (get_type_id(item) == LMD_TYPE_FLOAT) {
        // cast down to int
        dval = item.get_double();
        ival = (int64_t)dval;
        dval = (double)ival;  // truncate any fractional part
        CHECK_DVAL:
        if (dval > INT32_MAX || dval < INT32_MIN) {
            // keep as double
            return push_d(dval);
        }
        return {.item = i2it((int32_t)dval)};
    }
    else if (get_type_id(item) == LMD_TYPE_NUM_SIZED) {
        NumSizedType st = item.get_num_type();
        if (st == NUM_FLOAT16 || st == NUM_FLOAT32) {
            dval = item.get_num_sized_as_double();
            ival = (int64_t)dval;
            dval = (double)ival;
            goto CHECK_DVAL;
        }
        ival = item.get_num_sized_as_int64();
        if (ival > INT32_MAX || ival < INT32_MIN) return box_int64_value(ival);
        return {.item = i2it((int32_t)ival)};
    }
    else if (get_type_id(item) == LMD_TYPE_UINT64) {
        uint64_t uval = item.get_uint64();
        // Do not reinterpret a wide unsigned value as negative merely because
        // it crossed INT64_MAX; ordinary numeric conversion preserves value.
        if (uval > (uint64_t)INT64_MAX) return decimal_from_uint64(uval, context);
        if (uval > (uint64_t)INT32_MAX) return box_int64_value((int64_t)uval);
        return {.item = i2it((int32_t)uval)};
    }
    else if (get_type_id(item) == LMD_TYPE_DECIMAL) {
        // truncate fractional part and convert to int64
        int64_t result = decimal_to_int64(item);
        if (result == INT64_ERROR) return ItemError;
        if (result > INT32_MAX || result < INT32_MIN) {
            return box_int64_value(result);
        }
        return {.item = i2it((int32_t)result)};
    }
    else if (is_text_type_id(get_type_id(item))) {
        const char* chars = item.get_chars();
        uint32_t len = item.get_len();
        if (!chars || len == 0) {
            return ItemError;
        }
        char* endptr;
        // try to parse as int32 first
        errno = 0;  // clear errno before calling strtoll
        int32_t val = strtol(chars, &endptr, 10);
        if (endptr == chars) {
            log_debug("Cannot convert string '%s' to int", chars);
            return ItemError;
        }
        // check for overflow - if errno is set or we couldn't parse the full string
        if (errno == ERANGE || (*endptr != '\0')) {
            // try to parse as decimal
            return decimal_from_string(chars, context);
        }
        return (Item) { .item = i2it(val) };
    }
    else {
        log_debug("Cannot convert type %d to int", get_type_id(item));
        return ItemError;
    }
}

int64_t fn_int64(Item item) {
    // convert item to int64
    if (get_type_id(item) == LMD_TYPE_INT) {
        return item.get_int56();
    }
    else if (get_type_id(item) == LMD_TYPE_INT64) {
        return item.get_int64();
    }
    else if (get_type_id(item) == LMD_TYPE_FLOAT) {
        double dval = item.get_double();
        double truncated = (double)(int64_t)dval;
        if (truncated > LAMBDA_INT64_MAX || truncated < INT64_MIN) {
            log_debug("float value %g out of int64 range", dval);
            return INT64_ERROR;
        }
        return truncated;
    }
    else if (get_type_id(item) == LMD_TYPE_NUM_SIZED) {
        NumSizedType st = item.get_num_type();
        if (st == NUM_FLOAT16 || st == NUM_FLOAT32) {
            double dval = item.get_num_sized_as_double();
            return (int64_t)dval;
        }
        return item.get_num_sized_as_int64();
    }
    else if (get_type_id(item) == LMD_TYPE_UINT64) {
        return (int64_t)item.get_uint64();
    }
    else if (get_type_id(item) == LMD_TYPE_DECIMAL) {
        // Convert decimal to int64 using centralized function
        return decimal_to_int64(item);
    }
    else if (is_text_type_id(get_type_id(item))) {
        const char* chars = item.get_chars();
        uint32_t len = item.get_len();
        if (!chars || len == 0) {
            return 0;
        }
        char* endptr;
        errno = 0;  // clear errno before calling strtoll
        int64_t val = strtoll(chars, &endptr, 10);
        if (endptr == chars) {
            log_debug("Cannot convert string '%s' to int64", chars);
            return INT64_ERROR;
        }
        if (errno == ERANGE) {
            log_debug("String value '%s' out of int64 range", chars);
            return INT64_ERROR;
        }
        return val;
    }
    log_debug("Cannot convert type %d to int64", get_type_id(item));
    return INT64_ERROR;
}

// Constructor functions for type conversion

Item fn_decimal(Item item) {
    GUARD_ERROR1(item);
    // Convert item to decimal type using centralized decimal API
    if (get_type_id(item) == LMD_TYPE_DECIMAL) {
        return item;  // Already a decimal
    }
    else if (get_type_id(item) == LMD_TYPE_INT) {
        return decimal_from_int64(item.get_int56(), context);
    }
    else if (get_type_id(item) == LMD_TYPE_INT64) {
        return decimal_from_int64(item.get_int64(), context);
    }
    else if (get_type_id(item) == LMD_TYPE_FLOAT) {
        return decimal_from_double(item.get_double(), context);
    }
    else if (get_type_id(item) == LMD_TYPE_NUM_SIZED) {
        NumSizedType st = item.get_num_type();
        if (st == NUM_FLOAT16 || st == NUM_FLOAT32) {
            return decimal_from_double(item.get_num_sized_as_double(), context);
        }
        return decimal_from_int64(item.get_num_sized_as_int64(), context);
    }
    else if (get_type_id(item) == LMD_TYPE_UINT64) {
        return decimal_from_uint64(item.get_uint64(), context);
    }
    else if (is_text_type_id(get_type_id(item))) {
        const char* chars = item.get_chars();
        uint32_t len = item.get_len();
        if (!chars || len == 0) {
            log_debug("Cannot convert empty string/symbol to decimal");
            return ItemError;
        }
        // decimal_from_string needs null-terminated string
        char* null_term_str = (char*)mem_alloc(len + 1, MEM_CAT_EVAL);
        if (!null_term_str) {
            log_debug("Failed to allocate string buffer");
            return ItemError;
        }
        memcpy(null_term_str, chars, len);
        null_term_str[len] = '\0';
        Item result = decimal_from_string(null_term_str, context);
        mem_free(null_term_str);
        return result;
    }
    else {
        log_debug("Cannot convert type %d to decimal", get_type_id(item));
        return ItemError;
    }
}

static int format_native_numeric_scalar(Item item, char* buffer, size_t capacity) {
    TypeId type = get_type_id(item);
    switch (type) {
    case LMD_TYPE_INT:
        return snprintf(buffer, capacity, "%lld", (long long)item.get_int56());
    case LMD_TYPE_INT64:
        return snprintf(buffer, capacity, "%" PRId64, item.get_int64());
    case LMD_TYPE_UINT64:
        return snprintf(buffer, capacity, "%" PRIu64, item.get_uint64());
    case LMD_TYPE_FLOAT:
        return snprintf(buffer, capacity, "%.17g", item.get_double());
    default:
        return -1;
    }
}

Item fn_binary(Item item) {
    GUARD_ERROR1(item);
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_BINARY) {
        return item;
    }
    if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        const char* chars = item.get_chars();
        uint32_t len = item.get_len();
        if (!chars) {
            log_debug("fn_binary: cannot convert null text to binary");
            return ItemError;
        }
        Binary* bin = heap_binary_from_bytes(chars, len);
        return bin ? (Item){.item = x2it(bin)} : ItemError;
    }
    if (type_id == LMD_TYPE_INT || type_id == LMD_TYPE_INT64 ||
        type_id == LMD_TYPE_UINT64 || type_id == LMD_TYPE_FLOAT) {
        char buf[64];
        int len = format_native_numeric_scalar(item, buf, sizeof(buf));
        if (len < 0 || len >= (int)sizeof(buf)) return ItemError;
        Binary* bin = heap_binary_from_bytes(buf, len);
        return bin ? (Item){.item = x2it(bin)} : ItemError;
    }
    if (js_is_typed_array(item)) {
        return binary_from_typed_array(js_get_typed_array_ptr(item.map));
    }
    if (js_is_dataview(item)) {
        return binary_from_dataview(js_get_dataview_ptr(item));
    }
    log_debug("fn_binary: cannot convert type %d", type_id);
    return ItemError;
}

extern "C" Symbol* fn_symbol(Item item) {
    // Convert item to symbol type
    if (get_type_id(item) == LMD_TYPE_ERROR) {
        log_debug("fn_symbol: error item received");
        return NULL;
    }
    if (get_type_id(item) == LMD_TYPE_SYMBOL) {
        return item.get_safe_symbol();  // Already a symbol
    }
    else if (get_type_id(item) == LMD_TYPE_STRING) {
        // Convert string to symbol
        String* str = item.get_safe_string();
        if (!str) {
            log_debug("Cannot convert null string to symbol");
            return NULL;
        }

        Symbol* sym = heap_create_symbol(str->chars, str->len);
        if (!sym) {
            log_debug("Failed to allocate symbol");
            return NULL;
        }

        return sym;
    }
    else if (get_type_id(item) == LMD_TYPE_INT ||
             get_type_id(item) == LMD_TYPE_INT64 ||
             get_type_id(item) == LMD_TYPE_UINT64 ||
             get_type_id(item) == LMD_TYPE_FLOAT) {
        // Numeric-to-text conversion is shared with binary() so signed and
        // unsigned 64-bit carriers cannot drift again.
        char buf[64];
        int len = format_native_numeric_scalar(item, buf, sizeof(buf));
        if (len < 0 || len >= (int)sizeof(buf)) {
            log_debug("Failed to convert numeric value to symbol");
            return NULL;
        }

        Symbol* sym = heap_create_symbol(buf, len);
        if (!sym) {
            log_debug("Failed to allocate symbol");
            return NULL;
        }

        return sym;
    }
    else {
        log_debug("Cannot convert type %d to symbol", get_type_id(item));
        return NULL;
    }
}

// symbol(name, url) - create a namespaced symbol
// name: string or symbol - the local name
// url: string or symbol - the namespace URL
// fn_symbol2 - create namespaced symbol from name and URL
// Note: Must be declared in lambda.h for MIR C2MIR to know the signature
extern "C" Item fn_symbol2(Item name_item, Item url_item) {
    GUARD_ERROR2(name_item, url_item);

    // extract name string
    const char* name_str = nullptr;
    size_t name_len = 0;

    if (get_type_id(name_item) == LMD_TYPE_STRING) {
        String* str = name_item.get_safe_string();
        if (!str) {
            log_error("fn_symbol2: null name string");
            return ItemError;
        }
        name_str = str->chars;
        name_len = str->len;
    }
    else if (get_type_id(name_item) == LMD_TYPE_SYMBOL) {
        Symbol* sym = name_item.get_safe_symbol();
        if (!sym) {
            log_error("fn_symbol2: null name symbol");
            return ItemError;
        }
        name_str = sym->chars;
        name_len = sym->len;
    }
    else {
        log_error("fn_symbol2: name must be string or symbol, got type %d", get_type_id(name_item));
        return ItemError;
    }

    // convert url to Target
    Target* ns_target = item_to_target(url_item.item, nullptr);
    if (!ns_target) {
        log_error("fn_symbol2: failed to convert url to Target");
        return ItemError;
    }

    // create namespaced symbol
    Symbol* sym = (Symbol*)heap_alloc(sizeof(Symbol) + name_len + 1, LMD_TYPE_SYMBOL);
    if (!sym) {
        log_error("fn_symbol2: failed to allocate symbol");
        target_free(ns_target);
        return ItemError;
    }

    sym->len = name_len;
    sym->ns = ns_target;
    memcpy(sym->chars, name_str, name_len);
    sym->chars[name_len] = '\0';


    return (Item) { .item = y2it(sym) };
}

Item fn_float(Item item) {
    GUARD_ERROR1(item);
    // Convert item to float type
    if (get_type_id(item) == LMD_TYPE_FLOAT) {
        return item;  // Already a float
    }
    else if (get_type_id(item) == LMD_TYPE_INT) {
        int64_t int_val = item.get_int56();
        return push_d((double)int_val);
    }
    else if (get_type_id(item) == LMD_TYPE_INT64) {
        int64_t int_val = item.get_int64();
        return push_d((double)int_val);
    }
    else if (get_type_id(item) == LMD_TYPE_NUM_SIZED) {
        return push_d(item.get_num_sized_as_double());
    }
    else if (get_type_id(item) == LMD_TYPE_UINT64) {
        return push_d((double)item.get_uint64());
    }
    else if (get_type_id(item) == LMD_TYPE_DECIMAL) {
        double dval = decimal_to_double(item);
        return push_d(dval);
    }
    else if (is_text_type_id(get_type_id(item))) {
        const char* chars = item.get_chars();
        uint32_t len = item.get_len();
        if (!chars || len == 0) {
            log_debug("Empty string/symbol cannot be converted to float");
            return ItemError;
        }

        // Create a null-terminated copy of the string
        char* buf = (char*)mem_alloc(len + 1, MEM_CAT_EVAL);
        if (!buf) {
            log_debug("Failed to allocate buffer for string conversion");
            return ItemError;
        }
        memcpy(buf, chars, len);
        buf[len] = '\0';

        // Remove any commas from the string
        char* p = buf;
        char* q = buf;
        while (*p) {
            if (*p != ',') {
                *q++ = *p;
            }
            p++;
        }
        *q = '\0';

        char* endptr;
        errno = 0;
        double val = strtod(buf, &endptr);
        int saved_errno = errno;
        bool fully_parsed = (*endptr == '\0');
        mem_free(buf);

        if (saved_errno != 0 || !fully_parsed) {
            log_debug("Cannot convert string to float: %.*s", (int)len, chars);
            return ItemError;
        }

        return push_d(val);
    }
    else {
        log_debug("Cannot convert type %d to float", get_type_id(item));
        return ItemError;
    }
}
// ============================================================================
// UNBOXED SYSTEM FUNCTIONS (fn_*_u)
// These are native C implementations that bypass Item boxing overhead.
// They are called directly when types are known at compile time.
// ============================================================================

// --- Math functions (double → double) ---

// Note: sin, cos, tan, sqrt, log, log10, exp, fabs, floor, ceil, round
// are already handled by native_math_funcs in transpile.cpp which calls
// the C math library directly. We don't need wrappers for those.

// Power function: native double version
extern "C" double fn_pow_u(double base, double exponent) {
    return pow(base, exponent);
}

// Min/max for two doubles
extern "C" double fn_min2_u(double a, double b) {
    if (isnan(a) || isnan(b)) return NAN;
    if (a == 0.0 && b == 0.0) {
        // -0 < +0 per ES spec for Math.min
        return (signbit(a) || signbit(b)) ? -0.0 : 0.0;
    }
    return a < b ? a : b;
}

extern "C" double fn_max2_u(double a, double b) {
    if (isnan(a) || isnan(b)) return NAN;
    if (a == 0.0 && b == 0.0) {
        // +0 > -0 per ES spec for Math.max
        return (signbit(a) && signbit(b)) ? -0.0 : 0.0;
    }
    return a > b ? a : b;
}

// --- Integer operations ---

// Integer absolute value
extern "C" int64_t fn_abs_i(int64_t x) {
    if (x == INT64_MIN) return INT64_MIN;
    return x < 0 ? -x : x;
}

// Float absolute value (alternative to fabs)
extern "C" double fn_abs_f(double x) {
    return fabs(x);
}

// Integer negation
extern "C" int64_t fn_neg_i(int64_t x) {
    return int64_from_bits(0ULL - (uint64_t)x);
}

// Float negation
extern "C" double fn_neg_f(double x) {
    return -x;
}

// Integer modulo (handles div-by-zero: returns INT64_ERROR)
extern "C" int64_t fn_mod_i(int64_t a, int64_t b) {
    if (b == 0) {
        log_error("modulo by zero error");
        return INT64_ERROR;
    }
    if (a == INT64_MIN && b == -1) return 0;
    return a % b;
}

// Integer division (handles div-by-zero: returns INT64_ERROR)
extern "C" int64_t fn_idiv_i(int64_t a, int64_t b) {
    if (b == 0) {
        log_error("integer division by zero error");
        return INT64_ERROR;
    }
    if (a == INT64_MIN && b == -1) return INT64_MIN;
    return a / b;
}

// --- Boolean operations ---

// Logical not (native bool version)
extern "C" Bool fn_not_u(Bool x) {
    return !x;
}

// --- Comparison operations ---
// These are already handled by native C operators in transpile_binary_expr
// when both operands have known types, so no wrappers needed.

// --- Sign function ---
extern "C" int64_t fn_sign_i(int64_t x) {
    if (x > 0) return 1;
    if (x < 0) return -1;
    return 0;
}

extern "C" int64_t fn_sign_f(double x) {
    if (x > 0.0) return 1;
    if (x < 0.0) return -1;
    return 0;
}

// JS Math.round: rounds to +Infinity for ties (unlike C round which rounds away from zero)
// Math.round(-0.5) -> 0, Math.round(0.5) -> 1
extern "C" double js_math_round(double x) {
    // ES spec: if x in [-0.5, -0], return -0
    if (x != x || !isfinite(x) || x == 0.0) return x;
    if (fabs(x) >= 4503599627370496.0) return x;
    double int_part = 0.0;
    double frac = modf(x, &int_part);
    if (x < 0.0) {
        if (frac < -0.5) return int_part - 1.0;
        if (int_part == 0.0) return -0.0;
        return int_part;
    }
    return frac >= 0.5 ? int_part + 1.0 : int_part;
}

// JS-semantic Math functions that handle NaN, Infinity, -0 correctly.
// These operate on boxed Items and return boxed Items.
extern "C" Item js_math_trunc(Item x) {
    TypeId t = get_type_id(x);
    if (is_integer_type_id(t)) return x;
    if (t == LMD_TYPE_FLOAT) {
        double d = it2d(x);
        if (d != d) return push_d(NAN);              // NaN
        if (d == 0.0 || !isfinite(d)) return push_d(d); // -0, +0, +-Inf
        double r = trunc(d);
        if (r == 0.0 && signbit(r)) return push_d(-0.0); // preserve -0
        if (r >= -9007199254740991.0 && r <= 9007199254740991.0) {
            return (Item){.item = i2it((int64_t)r)};
        }
        return push_d(r);
    }
    return push_d(NAN);
}

extern "C" Item js_math_sign(Item x) {
    TypeId t = get_type_id(x);
    if (is_integer_type_id(t)) {
        int64_t v = it2i(x);
        if (v > 0) return (Item){.item = i2it(1)};
        if (v < 0) return (Item){.item = i2it(-1)};
        return (Item){.item = i2it(0)};
    }
    if (t == LMD_TYPE_FLOAT) {
        double d = it2d(x);
        if (d != d) return push_d(NAN);
        if (d == 0.0) return push_d(d); // preserves -0
        return (Item){.item = i2it(d > 0 ? 1 : -1)};
    }
    return push_d(NAN);
}

extern "C" Item js_math_floor(Item x) {
    TypeId t = get_type_id(x);
    if (is_integer_type_id(t)) return x;
    if (t == LMD_TYPE_FLOAT) {
        double d = it2d(x);
        if (d != d || !isfinite(d) || d == 0.0) return push_d(d);
        double r = floor(d);
        if (r >= -9007199254740991.0 && r <= 9007199254740991.0) {
            return (Item){.item = i2it((int64_t)r)};
        }
        return push_d(r);
    }
    return push_d(NAN);
}

// Double version for JIT native fast path — preserves -0
extern "C" double js_math_ceil_d(double d) {
    double r = ceil(d);
    if (r == 0.0 && d < 0.0) return -0.0;
    return r;
}

extern "C" Item js_math_ceil(Item x) {
    TypeId t = get_type_id(x);
    if (is_integer_type_id(t)) return x;
    if (t == LMD_TYPE_FLOAT) {
        double d = it2d(x);
        if (d != d || !isfinite(d) || d == 0.0) return push_d(d);
        double r = ceil(d);
        if (r == 0.0 && d < 0.0) return push_d(-0.0); // ceil(-0.5) → -0
        if (r >= -9007199254740991.0 && r <= 9007199254740991.0) {
            return (Item){.item = i2it((int64_t)r)};
        }
        return push_d(r);
    }
    return push_d(NAN);
}

extern "C" Item js_math_round_item(Item x) {
    TypeId t = get_type_id(x);
    if (is_integer_type_id(t)) return x;
    if (t == LMD_TYPE_FLOAT) {
        double d = it2d(x);
        if (d != d || !isfinite(d) || d == 0.0) return push_d(d);
        if (fabs(d) >= 4503599627370496.0) return push_d(d);
        double int_part = 0.0;
        double frac = modf(d, &int_part);
        double r;
        if (d < 0.0) {
            if (frac < -0.5) r = int_part - 1.0;
            else if (int_part == 0.0) r = -0.0;
            else r = int_part;
        } else {
            r = frac >= 0.5 ? int_part + 1.0 : int_part;
        }
        return push_d(r);
    }
    return push_d(NAN);
}

// --- Rounding functions (int version returns identity) ---
// floor/ceil/round of an integer is the integer itself
extern "C" int64_t fn_floor_i(int64_t x) {
    return x;
}

extern "C" int64_t fn_ceil_i(int64_t x) {
    return x;
}

extern "C" int64_t fn_round_i(int64_t x) {
    return x;
}

// ============================================================================
// BITWISE OPERATIONS
// ============================================================================

// Safe unbox to int64_t for bitwise operation arguments.
// Handles both tagged Items (type tag in high byte) and raw int64_t values
// (from other bitwise ops or literals, with high byte == 0).
extern "C" int64_t _barg(Item v) {
    uint8_t tag = get_type_id(v);
    switch (tag) {
    case LMD_TYPE_INT:
        return v.get_int56();
    case LMD_TYPE_INT64:
        return v.get_int64();
    case LMD_TYPE_FLOAT:
        return (int64_t)v.get_double();
    case LMD_TYPE_BOOL:
        return v.bool_val ? 1 : 0;
    case LMD_TYPE_NUM_SIZED:
        return v.get_num_sized_as_int64();
    case LMD_TYPE_UINT64:
        return (int64_t)v.get_uint64();
    case LMD_TYPE_RAW_POINTER:
        // raw int64_t (high byte == 0): from other bitwise ops or literals
        return (int64_t)v.item;
    case LMD_TYPE_NULL:
    case LMD_TYPE_ERROR:
        return 0;
    default:
        return 0;
    }
}

extern "C" int64_t fn_band(int64_t a, int64_t b) {
    return a & b;
}

extern "C" Item fn_band_item(Item a, Item b) {
    Item result;
    if (apply_classified_bitwise(a, b, SIZED_BITWISE_AND, &result)) return result;
    return box_int64_value(fn_band(_barg(a), _barg(b)));
}

extern "C" int64_t fn_bor(int64_t a, int64_t b) {
    return a | b;
}

extern "C" Item fn_bor_item(Item a, Item b) {
    Item result;
    if (apply_classified_bitwise(a, b, SIZED_BITWISE_OR, &result)) return result;
    return box_int64_value(fn_bor(_barg(a), _barg(b)));
}

extern "C" int64_t fn_bxor(int64_t a, int64_t b) {
    return a ^ b;
}

extern "C" Item fn_bxor_item(Item a, Item b) {
    Item result;
    if (apply_classified_bitwise(a, b, SIZED_BITWISE_XOR, &result)) return result;
    return box_int64_value(fn_bxor(_barg(a), _barg(b)));
}

extern "C" int64_t fn_bnot(int64_t a) {
    return ~a;
}

extern "C" Item fn_bnot_item(Item a) {
    Item result;
    if (sized_bitwise_not(a, &result)) return result;
    if (lambda_numeric_kind_from_item(a) == LAMBDA_NUM_INTEGER) {
        // Integer is unbounded, so preserve its two's-complement identity in
        // the bigint domain instead of truncating it through an int64 lane.
        return bigint_bitwise_not(a);
    }
    return box_int64_value(fn_bnot(_barg(a)));
}

extern "C" int64_t fn_shl(int64_t a, int64_t b) {
    if (b < 0) {
        log_error("integer negative shift count");
        return INT64_ERROR;
    }
    if (b >= 64) return 0;
    return a << b;
}

extern "C" Item fn_shl_item(Item a, Item b) {
    Item result;
    if (apply_classified_shift(a, b, true, &result)) return result;
    if (_barg(b) < 0) {
        log_error("integer negative shift count");
        return ItemError;
    }
    return box_int64_value(fn_shl(_barg(a), _barg(b)));
}

extern "C" int64_t fn_shr(int64_t a, int64_t b) {
    if (b < 0) {
        log_error("integer negative shift count");
        return INT64_ERROR;
    }
    if (b >= 64) return 0;
    return a >> b;  // arithmetic right shift (sign-extending)
}

extern "C" Item fn_shr_item(Item a, Item b) {
    Item result;
    if (apply_classified_shift(a, b, false, &result)) return result;
    if (_barg(b) < 0) {
        log_error("integer negative shift count");
        return ItemError;
    }
    return box_int64_value(fn_shr(_barg(a), _barg(b)));
}

extern "C" Item fn_ushr_item(Item a, Item b) {
    BitwiseIntegerValue left, right;
    read_bitwise_integer(a, &left);
    read_bitwise_integer(b, &right);
    int64_t count = right.value.sval;
    if (count < 0) {
        log_error("integer unsigned right shift negative count");
        return ItemError;
    }

    // ushr is defined in the left operand's unsigned width. An int64 must not
    // pass through a signed native shift, which would sign-extend its high bit.
    int bits = left.is_sized ? left.value.bits : 32;
    uint64_t raw = left.is_sized
        ? sized_operand_raw(&left.value) & sized_mask(bits)
        : (uint32_t)_barg(a);
    raw = count >= bits ? 0 : raw >> count;
    return pack_sized_integer(true, bits, raw);
}
