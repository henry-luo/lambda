
#include "transpiler.hpp"
#include "../lib/log.h"
#include <stdarg.h>
#include <time.h>
#include <cstdlib>  // for abs function
#include <cmath>    // for pow function
#include <errno.h>  // for errno checking
#include <inttypes.h>  // for PRId64

extern __thread EvalContext* context;

String* str_repeat(String* str, int64_t times);
String* fn_strcat(String* left, String* right);

// Helper functions for decimal operations
Item push_decimal(mpd_t* dec_val) {
    if (!dec_val) return ItemError;

    Decimal* decimal = (Decimal*)heap_alloc(sizeof(Decimal), LMD_TYPE_DECIMAL);
    if (!decimal) {
        mpd_del(dec_val);
        return ItemError;
    }

    decimal->ref_cnt = 1;
    decimal->dec_val = dec_val;

    Item result;
    result.item = c2it(decimal);
    return result;
}

Item push_c(int64_t cval) {
    if (cval == INT64_ERROR) { return ItemError; }
    mpd_t* result = mpd_new(context->decimal_ctx);
    if (!result) return ItemError;
    mpd_set_ssize(result, cval, context->decimal_ctx);
    return push_decimal(result);
}

mpd_t* convert_to_decimal(Item item, mpd_context_t* ctx) {
    if (item._type_id == LMD_TYPE_DECIMAL) {
        Decimal* dec_ptr = item.get_decimal();
        return dec_ptr->dec_val;
    }

    mpd_t* result = mpd_new(ctx);
    if (!result) return NULL;

    if (item._type_id == LMD_TYPE_INT) {
        mpd_set_ssize(result, item.get_int56(), ctx);
    }
    else if (item._type_id == LMD_TYPE_INT64) {
        int64_t val = item.get_int64();
        mpd_set_ssize(result, val, ctx);
    }
    else if (item._type_id == LMD_TYPE_FLOAT) {
        double val = item.get_double();
        char str_buf[64];
        snprintf(str_buf, sizeof(str_buf), "%.17g", val);
        mpd_set_string(result, str_buf, ctx);
    }
    else {
        mpd_del(result);
        return NULL;
    }
    return result;
}

void cleanup_temp_decimal(mpd_t* dec_val, bool is_original_decimal) {
    // Only delete if this was a temporary decimal we created
    if (!is_original_decimal && dec_val) {
        mpd_del(dec_val);
    }
}

bool decimal_is_zero(mpd_t* dec_val) {
    return mpd_iszero(dec_val);
}

Item fn_add(Item item_a, Item item_b) {
    TypeId type_a = get_type_id(item_a);  TypeId type_b = get_type_id(item_b);
    log_debug("fn_add called with types: %d and %d", type_a, type_b);
    if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_INT) {
        int64_t a = item_a.get_int56();
        int64_t b = item_b.get_int56();
        log_debug("add int + int: %lld + %lld", a, b);
        // use __builtin_add_overflow for efficient overflow detection
#if defined(__GNUC__) || defined(__clang__)
        int64_t result;
        if (__builtin_add_overflow(a, b, &result) || result > INT56_MAX || result < INT56_MIN) {
            log_error("integer overflow in addition");
            return ItemError;
        }
#else
        int64_t result = a + b;
        if (result > INT56_MAX || result < INT56_MIN) {
            log_error("integer overflow in addition");
            return ItemError;
        }
#endif
        return { .item = i2it(result) };
    }
    else if (type_a == LMD_TYPE_INT64 && type_b == LMD_TYPE_INT64) {
        return push_l(item_a.get_int64() + item_b.get_int64());
    }
    else if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_INT64) {
        return push_l(item_a.get_int56() + item_b.get_int64());
    }
    else if (type_a == LMD_TYPE_INT64 && type_b == LMD_TYPE_INT) {
        return push_l(item_a.get_int64() + item_b.get_int56());
    }
    else if (type_a == LMD_TYPE_FLOAT && type_b == LMD_TYPE_FLOAT) {
        log_debug("add float: %g + %g", item_a.get_double(), item_b.get_double());
        return push_d(item_a.get_double() + item_b.get_double());
    }
    else if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_FLOAT) {
        log_debug("add int + float: %lld + %g", item_a.get_int56(), item_b.get_double());
        return push_d((double)item_a.get_int56() + item_b.get_double());
    }
    else if (type_a == LMD_TYPE_FLOAT && type_b == LMD_TYPE_INT) {
        return push_d(item_a.get_double() + (double)item_b.get_int56());
    }
    else if (type_a == LMD_TYPE_INT64 && type_b == LMD_TYPE_FLOAT) {
        return push_d((double)item_a.get_int64() + item_b.get_double());
    }
    else if (type_a == LMD_TYPE_FLOAT && type_b == LMD_TYPE_INT64) {
        return push_d(item_a.get_double() + (double)item_b.get_int64());
    }
    // decimal support
    else if (type_a == LMD_TYPE_DECIMAL || type_b == LMD_TYPE_DECIMAL) {
        log_debug("fn_add: decimal addition");
        mpd_t* a_dec = convert_to_decimal(item_a, context->decimal_ctx);
        mpd_t* b_dec = convert_to_decimal(item_b, context->decimal_ctx);

        if (!a_dec || !b_dec) {
            if (a_dec) cleanup_temp_decimal(a_dec, item_a._type_id);
            if (b_dec) cleanup_temp_decimal(b_dec, item_b._type_id);
            log_error("decimal conversion failed in fn_add");
            return ItemError;
        }

        mpd_t* result = mpd_new(context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(a_dec, item_a._type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b._type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }

        mpd_add(result, a_dec, b_dec, context->decimal_ctx);

        cleanup_temp_decimal(a_dec, item_a._type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(b_dec, item_b._type_id == LMD_TYPE_DECIMAL);

        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            log_error("decimal addition failed");
            return ItemError;
        }
        return push_decimal(result);
    }

    // vectorized addition for Arrays
    // todo: array + number
    // ArrayInt and ArrayInt64 support
    else if (type_a == LMD_TYPE_ARRAY_INT && type_b == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr_a = item_a.array_int;
        ArrayInt* arr_b = item_b.array_int;
        if (arr_a->length != arr_b->length) {
            log_error("Array length mismatch in addition");
            return ItemError;
        }
        ArrayInt* result = array_int_new(arr_a->length);
        for (int64_t i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] + arr_b->items[i];
        }
        return { .array_int = result };
    }
    // ArrayInt64 support
    else if (type_a == LMD_TYPE_ARRAY_INT64 && type_b == LMD_TYPE_ARRAY_INT64) {
        log_debug("fn_add: ArrayInt64 addition");
        ArrayInt64* arr_a = item_a.array_int64;
        ArrayInt64* arr_b = item_b.array_int64;
        if (arr_a->length != arr_b->length) {
            log_error("Array length mismatch in addition");
            return ItemError;
        }
        ArrayInt64* result = array_int64_new(arr_a->length);
        for (int64_t i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] + arr_b->items[i];
        }
        log_debug("fn_add: returning ArrayInt64 result %p, length: %ld, type: %d", result, result->length, result->type_id);
        return { .array_int64 = result };
    }
    // ArrayFloat support
    else if (type_a == LMD_TYPE_ARRAY_FLOAT && type_b == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr_a = item_a.array_float;
        ArrayFloat* arr_b = item_b.array_float;
        if (arr_a->length != arr_b->length) {
            log_error("Array length mismatch in addition");
            return ItemError;
        }
        ArrayFloat* result = array_float_new(arr_a->length);
        for (int64_t i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] + arr_b->items[i];
        }
        return { .array_float = result };
    }
    // todo: mixed array types
    else {
        log_error("unknown add type: %d, %d", item_a._type_id, item_b._type_id);
    }
    return ItemError;
}

Item fn_mul(Item item_a, Item item_b) {
    TypeId type_a = get_type_id(item_a);  TypeId type_b = get_type_id(item_b);
    if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_INT) {
        int64_t a = item_a.get_int56();
        int64_t b = item_b.get_int56();
        // use __builtin_mul_overflow for efficient overflow detection
#if defined(__GNUC__) || defined(__clang__)
        int64_t result;
        if (__builtin_mul_overflow(a, b, &result) || result > INT56_MAX || result < INT56_MIN) {
            log_error("integer overflow in multiplication");
            return ItemError;
        }
#else
        // fallback: use __int128 for precise overflow detection
        __int128 wide_result = (__int128)a * (__int128)b;
        if (wide_result > INT56_MAX || wide_result < INT56_MIN) {
            log_error("integer overflow in multiplication");
            return ItemError;
        }
        int64_t result = (int64_t)wide_result;
#endif
        return { .item = i2it(result) };
    }
    else if (type_a == LMD_TYPE_INT64 && type_b == LMD_TYPE_INT64) {
        return push_l(item_a.get_int64() * item_b.get_int64());
    }
    else if (type_a == LMD_TYPE_FLOAT && type_b == LMD_TYPE_FLOAT) {
        log_debug("mul float: %g * %g\n", item_a.get_double(), item_b.get_double());
        return push_d(item_a.get_double() * item_b.get_double());
    }
    else if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_FLOAT) {
        return push_d((double)item_a.get_int56() * item_b.get_double());
    }
    else if (type_a == LMD_TYPE_FLOAT && type_b == LMD_TYPE_INT) {
        return push_d(item_a.get_double() * (double)item_b.get_int56());
    }
    // else if (type_a == LMD_TYPE_STRING && type_b == LMD_TYPE_INT) {
    //     String* str_a = (String*)item_a.pointer;
    //     String* result = str_repeat(str_a, item_b.int_val);
    //     return { .item = s2it(result) };
    // }
    // else if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_STRING) {
    //     String* str_b = (String*)item_b.pointer;
    //     String* result = str_repeat(str_b, item_a.int_val);
    //     return { .item = s2it(result) };
    // }
    // Add libmpdec decimal support
    else if (type_a == LMD_TYPE_DECIMAL || type_b == LMD_TYPE_DECIMAL) {
        mpd_t* a_dec = convert_to_decimal(item_a, context->decimal_ctx);
        mpd_t* b_dec = convert_to_decimal(item_b, context->decimal_ctx);

        if (!a_dec || !b_dec) {
            if (a_dec) cleanup_temp_decimal(a_dec, item_a._type_id);
            if (b_dec) cleanup_temp_decimal(b_dec, item_b._type_id);
            log_error("decimal conversion failed in fn_mul");
            return ItemError;
        }

        mpd_t* result = mpd_new(context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(a_dec, item_a._type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b._type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }

        mpd_mul(result, a_dec, b_dec, context->decimal_ctx);

        cleanup_temp_decimal(a_dec, item_a._type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(b_dec, item_b._type_id == LMD_TYPE_DECIMAL);

        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            log_error("decimal multiplication failed");
            return ItemError;
        }

        return push_decimal(result);
    }
    else if (type_a == LMD_TYPE_ARRAY_INT && type_b == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr_a = item_a.array_int;
        ArrayInt* arr_b = item_b.array_int;
        if (arr_a->length != arr_b->length) {
            log_error("Array length mismatch in multiplication");
            return ItemError;
        }
        ArrayInt* result = array_int_new(arr_a->length);
        for (int64_t i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] * arr_b->items[i];
        }
        return { .array_int = result };
    }
    else if (type_a == LMD_TYPE_ARRAY_INT64 && type_b == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr_a = item_a.array_int64;
        ArrayInt64* arr_b = item_b.array_int64;
        if (arr_a->length != arr_b->length) {
            log_error("Array length mismatch in multiplication");
            return ItemError;
        }
        ArrayInt64* result = array_int64_new(arr_a->length);
        for (int64_t i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] * arr_b->items[i];
        }
        return { .array_int64 = result };
    }
    else if (type_a == LMD_TYPE_ARRAY_FLOAT && type_b == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr_a = item_a.array_float;
        ArrayFloat* arr_b = item_b.array_float;
        if (arr_a->length != arr_b->length) {
            log_error("Array length mismatch in multiplication");
            return ItemError;
        }
        ArrayFloat* result = array_float_new(arr_a->length);
        for (int64_t i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] * arr_b->items[i];
        }
        return { .array_float = result };
    }
    else {
        log_error("unknown mul type: %d, %d", item_a._type_id, item_b._type_id);
    }
    return ItemError;
}

Item fn_sub(Item item_a, Item item_b) {
    TypeId type_a = get_type_id(item_a);  TypeId type_b = get_type_id(item_b);
    if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_INT) {
        int64_t a = item_a.get_int56();
        int64_t b = item_b.get_int56();
        // use __builtin_sub_overflow for efficient overflow detection
#if defined(__GNUC__) || defined(__clang__)
        int64_t result;
        if (__builtin_sub_overflow(a, b, &result) || result > INT56_MAX || result < INT56_MIN) {
            log_error("integer overflow in subtraction");
            return ItemError;
        }
#else
        int64_t result = a - b;
        if (result > INT56_MAX || result < INT56_MIN) {
            log_error("integer overflow in subtraction");
            return ItemError;
        }
#endif
        return { .item = i2it(result) };
    }
    else if (type_a == LMD_TYPE_INT64 && type_b == LMD_TYPE_INT64) {
        return push_l(item_a.get_int64() - item_b.get_int64());
    }
    else if (type_a == LMD_TYPE_FLOAT && type_b == LMD_TYPE_FLOAT) {
        log_debug("sub float: %g - %g", item_a.get_double(), item_b.get_double());
        return push_d(item_a.get_double() - item_b.get_double());
    }
    else if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_FLOAT) {
        return push_d((double)item_a.get_int56() - item_b.get_double());
    }
    else if (type_a == LMD_TYPE_FLOAT && type_b == LMD_TYPE_INT) {
        return push_d(item_a.get_double() - (double)item_b.get_int56());
    }
    else if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_INT64) {
        return push_l(item_a.get_int56() - item_b.get_int64());
    }
    else if (type_a == LMD_TYPE_INT64 && type_b == LMD_TYPE_INT) {
        return push_l(item_a.get_int64() - item_b.get_int56());
    }
    // Add libmpdec decimal support
    else if (type_a == LMD_TYPE_DECIMAL || type_b == LMD_TYPE_DECIMAL) {
        mpd_t* a_dec = convert_to_decimal(item_a, context->decimal_ctx);
        mpd_t* b_dec = convert_to_decimal(item_b, context->decimal_ctx);

        if (!a_dec || !b_dec) {
            if (a_dec) cleanup_temp_decimal(a_dec, item_a._type_id);
            if (b_dec) cleanup_temp_decimal(b_dec, item_b._type_id);
            log_error("decimal conversion failed in fn_sub");
            return ItemError;
        }

        mpd_t* result = mpd_new(context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(a_dec, item_a._type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b._type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }

        mpd_sub(result, a_dec, b_dec, context->decimal_ctx);

        cleanup_temp_decimal(a_dec, item_a._type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(b_dec, item_b._type_id == LMD_TYPE_DECIMAL);

        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            log_error("decimal subtraction failed");
            return ItemError;
        }

        return push_decimal(result);
    }
    else if (type_a == LMD_TYPE_ARRAY_INT && type_b == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr_a = item_a.array_int;
        ArrayInt* arr_b = item_b.array_int;
        if (arr_a->length != arr_b->length) {
            log_error("Array length mismatch in subtraction");
            return ItemError;
        }
        ArrayInt* result = array_int_new(arr_a->length);
        for (int64_t i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] - arr_b->items[i];
        }
        return { .array_int = result };
    }
    else if (type_a == LMD_TYPE_ARRAY_INT64 && type_b == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr_a = item_a.array_int64;
        ArrayInt64* arr_b = item_b.array_int64;
        if (arr_a->length != arr_b->length) {
            log_error("Array length mismatch in subtraction");
            return ItemError;
        }
        ArrayInt64* result = array_int64_new(arr_a->length);
        for (int64_t i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] - arr_b->items[i];
        }
        return { .array_int64 = result };
    }
    else if (type_a == LMD_TYPE_ARRAY_FLOAT && type_b == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr_a = item_a.array_float;
        ArrayFloat* arr_b = item_b.array_float;
        if (arr_a->length != arr_b->length) {
            log_error("Array length mismatch in subtraction");
            return ItemError;
        }
        ArrayFloat* result = array_float_new(arr_a->length);
        for (int64_t i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] - arr_b->items[i];
        }
        return { .array_float = result };
    }
    else {
        log_error("unknown sub type: %d, %d", item_a._type_id, item_b._type_id);
    }
    return ItemError;
}

Item fn_div(Item item_a, Item item_b) {
    TypeId type_a = get_type_id(item_a);  TypeId type_b = get_type_id(item_b);
    if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_INT) {
        int64_t b = item_b.get_int56();
        if (b == 0) {
            log_error("division by zero error");
            return ItemError;
        }
        // always promote to double for division
        return push_d((double)item_a.get_int56() / (double)b);
    }
    else if (type_a == LMD_TYPE_INT64 && type_b == LMD_TYPE_INT64) {
        if (item_b.get_int64() == 0) {
            log_error("division by zero error");
            return ItemError;
        }
        return push_d((double)item_a.get_int64() / (double)item_b.get_int64());
    }
    else if (type_a == LMD_TYPE_FLOAT && type_b == LMD_TYPE_FLOAT) {
        if (item_b.get_double() == 0.0) {
            log_error("division by zero error");
            return ItemError;
        }
        log_debug("div float: %g / %g\n", item_a.get_double(), item_b.get_double());
        return push_d(item_a.get_double() / item_b.get_double());
    }
    else if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_FLOAT) {
        if (item_b.get_double() == 0.0) {
            log_error("division by zero error");
            return ItemError;
        }
        return push_d((double)item_a.get_int56() / item_b.get_double());
    }
    else if (type_a == LMD_TYPE_FLOAT && type_b == LMD_TYPE_INT) {
        int64_t b = item_b.get_int56();
        if (b == 0) {
            log_error("division by zero error");
            return ItemError;
        }
        return push_d(item_a.get_double() / (double)b);
    }
    else if (type_a == LMD_TYPE_INT64 && type_b == LMD_TYPE_FLOAT) {
        int64_t a_val = item_a.get_int64();
        if (item_b.get_double() == 0.0) {
            log_error("float division by zero error");
            return ItemError;
        }
        return push_d((double)a_val / item_b.get_double());
    }
    else if (type_a == LMD_TYPE_FLOAT && type_b == LMD_TYPE_INT64) {
        int64_t b_val = item_b.get_int64();
        if (b_val == 0) {
            log_error("division by zero error");
            return ItemError;
        }
        return push_d(item_a.get_double() / (double)b_val);
    }
    else if (type_a == LMD_TYPE_INT && type_b == LMD_TYPE_INT64) {
        if (item_b.get_int64() == 0) {
            log_error("division by zero error");
            return ItemError;
        }
        return push_d((double)item_a.get_int56() / (double)item_b.get_int64());
    }
    else if (type_a == LMD_TYPE_INT64 && type_b == LMD_TYPE_INT) {
        int64_t b = item_b.get_int56();
        if (b == 0) {
            log_error("division by zero error");
            return ItemError;
        }
        return push_d((double)item_a.get_int64() / (double)b);
    }
    // Add libmpdec decimal support
    else if (type_a == LMD_TYPE_DECIMAL || type_b == LMD_TYPE_DECIMAL) {
        mpd_t* a_dec = convert_to_decimal(item_a, context->decimal_ctx);
        mpd_t* b_dec = convert_to_decimal(item_b, context->decimal_ctx);

        if (!a_dec || !b_dec) {
            if (a_dec) cleanup_temp_decimal(a_dec, item_a._type_id);
            if (b_dec) cleanup_temp_decimal(b_dec, item_b._type_id);
            log_error("decimal conversion failed in fn_div");
            return ItemError;
        }

        // Check for division by zero
        log_debug(" Checking division by zero, b_dec=%p\n", b_dec);
        if (b_dec) {
            char* b_str = mpd_to_sci(b_dec, 1);
            log_debug(" b_dec value as string: '%s'\n", b_str ? b_str : "NULL");
            log_debug(" mpd_iszero result: %d\n", mpd_iszero(b_dec));
            if (b_str) mpd_free(b_str);
        }
        if (b_dec && decimal_is_zero(b_dec)) {
            cleanup_temp_decimal(a_dec, item_a._type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b._type_id == LMD_TYPE_DECIMAL);
            log_error("decimal division by zero error");
            return ItemError;
        }
        log_debug(" Division by zero check passed\n");

        // Debug the values and context before division
        if (a_dec && b_dec) {
            char* a_str = mpd_to_sci(a_dec, 1);
            char* b_str = mpd_to_sci(b_dec, 1);
            log_debug(" About to divide '%s' / '%s'\n", a_str ? a_str : "NULL", b_str ? b_str : "NULL");
            log_debug(" Context prec=%lld, emax=%lld, emin=%lld\n", (long long)context->decimal_ctx->prec, (long long)context->decimal_ctx->emax, (long long)context->decimal_ctx->emin);
            if (a_str) mpd_free(a_str);
            if (b_str) mpd_free(b_str);
        }

        mpd_t* result = mpd_new(context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(a_dec, item_a._type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b._type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }

        log_debug(" Calling mpd_div...\n");
        mpd_div(result, a_dec, b_dec, context->decimal_ctx);
        log_debug(" mpd_div completed\n");

        cleanup_temp_decimal(a_dec, item_a._type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(b_dec, item_b._type_id == LMD_TYPE_DECIMAL);

        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            log_error("decimal division failed");
            return ItemError;
        }

        return push_decimal(result);
    }
    else if (type_a == LMD_TYPE_ARRAY_INT && type_b == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr_a = item_a.array_int;
        ArrayInt* arr_b = item_b.array_int;
        if (arr_a->length != arr_b->length) {
            log_error("Array length mismatch in division");
            return ItemError;
        }
        ArrayFloat* result = array_float_new(arr_a->length);
        for (int64_t i = 0; i < arr_a->length; i++) {
            if (arr_b->items[i] == 0) {
                log_error("integer division by zero error in array element %" PRId64, i);
                free(result->items);
                free(result);
                return ItemError;
            }
            result->items[i] = (double)arr_a->items[i] / (double)arr_b->items[i];
        }
        return { .array_float = result };
    }
    else if (type_a == LMD_TYPE_ARRAY_INT64 && type_b == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr_a = item_a.array_int64;
        ArrayInt64* arr_b = item_b.array_int64;
        if (arr_a->length != arr_b->length) {
            log_error("Array length mismatch in division");
            return ItemError;
        }
        ArrayFloat* result = array_float_new(arr_a->length);
        for (int64_t i = 0; i < arr_a->length; i++) {
            if (arr_b->items[i] == 0) {
                log_error("integer division by zero error in array element %" PRId64, i);
                free(result->items);
                free(result);
                return ItemError;
            }
            result->items[i] = (double)arr_a->items[i] / (double)arr_b->items[i];
        }
        return { .array_float = result };
    }
    else if (type_a == LMD_TYPE_ARRAY_FLOAT && type_b == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr_a = item_a.array_float;
        ArrayFloat* arr_b = item_b.array_float;
        if (arr_a->length != arr_b->length) {
            log_error("Array length mismatch in division");
            return ItemError;
        }
        ArrayFloat* result = array_float_new(arr_a->length);
        for (int64_t i = 0; i < arr_a->length; i++) {
            if (arr_b->items[i] == 0.0) {
                log_error("float division by zero error in array element %" PRId64, i);
                free(result->items);
                free(result);
                return ItemError;
            }
            result->items[i] = arr_a->items[i] / arr_b->items[i];
        }
        return { .array_float = result };
    }
    else {
        log_error("unknown div type: %d, %d\n", item_a._type_id, item_b._type_id);
    }
    return ItemError;
}

Item fn_idiv(Item item_a, Item item_b) {
    // Check for division by zero
    bool is_zero = false;
    if (item_b._type_id == LMD_TYPE_INT) {
        is_zero = (item_b.get_int56() == 0);
    }
    else if (item_b._type_id == LMD_TYPE_INT64 && item_b.get_int64() == 0) {
        is_zero = true;
    }

    if (is_zero) {
        log_error("integer division by zero error");
        return ItemError;
    }

    if (item_a._type_id == LMD_TYPE_INT && item_b._type_id == LMD_TYPE_INT) {
        int64_t a_val = item_a.get_int56();
        int64_t b_val = item_b.get_int56();
        return (Item) { .item = i2it(a_val / b_val) };
    }
    else if (item_a._type_id == LMD_TYPE_INT64 && item_b._type_id == LMD_TYPE_INT64) {
        return push_l(item_a.get_int64() / item_b.get_int64());
    }
    else if (item_a._type_id == LMD_TYPE_INT && item_b._type_id == LMD_TYPE_INT64) {
        int64_t a_val = item_a.get_int56();
        return push_l(a_val / item_b.get_int64());
    }
    else if (item_a._type_id == LMD_TYPE_INT64 && item_b._type_id == LMD_TYPE_INT) {
        int64_t b_val = item_b.get_int56();
        return push_l(item_a.get_int64() / b_val);
    }
    else {
        log_error("unknown idiv type: %d, %d\n", item_a._type_id, item_b._type_id);
    }
    return ItemError;
}

Item fn_pow(Item item_a, Item item_b) {
    log_debug("fn_pow called with types: %d and %d", item_a._type_id, item_b._type_id);
    if (item_a._type_id == LMD_TYPE_DECIMAL || item_b._type_id == LMD_TYPE_DECIMAL) {
        // For now, convert decimals to double for power operations
        // This is a limitation of libmpdec - it doesn't have general power operations
        double base = 0.0, exponent = 0.0;

        if (item_a._type_id == LMD_TYPE_DECIMAL) {
            Decimal* dec_ptr = item_a.get_decimal();
            // Convert decimal to string then to double (preserves precision better)
            char* str = mpd_to_sci(dec_ptr->dec_val, 1);
            base = strtod(str, NULL);
            mpd_free(str);
        }
        else {
            // Convert non-decimal to double
            if (item_a._type_id == LMD_TYPE_INT) {
                base = (double)item_a.get_int56();
            }
            else if (item_a._type_id == LMD_TYPE_INT64) {
                base = (double)item_a.get_int64();
            }
            else if (item_a._type_id == LMD_TYPE_FLOAT) {
                base = item_a.get_double();
            }
            else {
                log_error("unsupported pow base type with decimal: %d", item_a._type_id);
                return ItemError;
            }
        }

        if (item_b._type_id == LMD_TYPE_DECIMAL) {
            Decimal* dec_ptr = item_b.get_decimal();
            char* str = mpd_to_sci(dec_ptr->dec_val, 1);
            exponent = strtod(str, NULL);
            mpd_free(str);
        }
        else {
            // Convert non-decimal to double
            if (item_b._type_id == LMD_TYPE_INT) {
                exponent = (double)item_b.get_int56();
            }
            else if (item_b._type_id == LMD_TYPE_INT64) {
                exponent = (double)item_b.get_int64();
            }
            else if (item_b._type_id == LMD_TYPE_FLOAT) {
                exponent = item_b.get_double();
            }
            else {
                log_error("unsupported pow exponent type with decimal: %d", item_b._type_id);
                return ItemError;
            }
        }

        // For decimal operations, return a decimal result
        double result_val = pow(base, exponent);

        mpd_t* result = mpd_new(context->decimal_ctx);
        if (!result) return ItemError;

        char str_buf[64];
        snprintf(str_buf, sizeof(str_buf), "%.17g", result_val);
        mpd_set_string(result, str_buf, context->decimal_ctx);

        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            log_debug("decimal power operation failed");
            return ItemError;
        }
        return push_decimal(result);
    }

    // non-decimal logic
    double base = 0.0, exponent = 0.0;

    // convert first argument to double
    if (item_a._type_id == LMD_TYPE_INT) {
        base = (double)item_a.get_int56();
    }
    else if (item_a._type_id == LMD_TYPE_INT64) {
        base = (double)item_a.get_int64();
    }
    else if (item_a._type_id == LMD_TYPE_FLOAT) {
        base = item_a.get_double();
    }
    else {
        log_error("unknown pow base type: %d", item_a._type_id);
        return ItemError;
    }

    // convert second argument to double
    if (item_b._type_id == LMD_TYPE_INT) {
        exponent = (double)item_b.get_int56();
    }
    else if (item_b._type_id == LMD_TYPE_INT64) {
        exponent = (double)item_b.get_int64();
    }
    else if (item_b._type_id == LMD_TYPE_FLOAT) {
        exponent = item_b.get_double();
    }
    else {
        log_error("unknown pow exponent type: %d", item_b._type_id);
        return ItemError;
    }
    log_debug("calculating pow base=%g, exponent=%g", base, exponent);
    return push_d(pow(base, exponent));
}

Item fn_mod(Item item_a, Item item_b) {
    // Handle decimal types first
    if (item_a._type_id == LMD_TYPE_DECIMAL || item_b._type_id == LMD_TYPE_DECIMAL) {
        mpd_t* val_a = convert_to_decimal(item_a, context->decimal_ctx);
        if (!val_a) return ItemError;

        mpd_t* val_b = convert_to_decimal(item_b, context->decimal_ctx);
        if (!val_b) {
            cleanup_temp_decimal(val_a, item_a._type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }

        // Check for division by zero
        if (decimal_is_zero(val_b)) {
            log_error("modulo by zero error");
            cleanup_temp_decimal(val_a, item_a._type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(val_b, item_b._type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }

        mpd_t* result = mpd_new(context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(val_a, item_a._type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(val_b, item_b._type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }

        mpd_rem(result, val_a, val_b, context->decimal_ctx);

        // Clean up temporary decimals
        cleanup_temp_decimal(val_a, item_a._type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(val_b, item_b._type_id == LMD_TYPE_DECIMAL);

        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            log_debug("decimal modulo operation failed");
            return ItemError;
        }

        return push_decimal(result);
    }

    // Original non-decimal logic for integer mod
    if (item_a._type_id == LMD_TYPE_INT && item_b._type_id == LMD_TYPE_INT) {
        int64_t a_val = item_a.get_int56();
        int64_t b_val = item_b.get_int56();
        if (b_val == 0) {
            log_error("modulo by zero error");
            return ItemError;
        }
        return (Item) { .item = i2it(a_val % b_val) };
    }
    else if (item_a._type_id == LMD_TYPE_INT64 && item_b._type_id == LMD_TYPE_INT64) {
        int64_t a_val = item_a.get_int64(), b_val = item_b.get_int64();
        if (b_val == 0) {
            log_error("modulo by zero error");
            return ItemError;
        }
        return push_l(a_val % b_val);
    }
    else if (item_a._type_id == LMD_TYPE_INT && item_b._type_id == LMD_TYPE_INT64) {
        int64_t a_val = item_a.get_int56(), b_val = item_b.get_int64();
        if (b_val == 0) {
            log_error("modulo by zero error");
            return ItemError;
        }
        return push_l(a_val % b_val);
    }
    else if (item_a._type_id == LMD_TYPE_INT64 && item_b._type_id == LMD_TYPE_INT) {
        int64_t a_val = item_a.get_int64(), b_val = item_b.get_int56();
        if (b_val == 0) {
            log_error("modulo by zero error");
            return ItemError;
        }
        return push_l(a_val % b_val);
    }
    else if (item_a._type_id == LMD_TYPE_FLOAT || item_b._type_id == LMD_TYPE_FLOAT) {
        log_debug("modulo not supported for float types");
        return ItemError;
    }
    else {
        log_error("unknown mod type: %d, %d\n", item_a._type_id, item_b._type_id);
        return ItemError;
    }
}

// Numeric system functions implementation

Item fn_abs(Item item) {
    // abs() - absolute value of a number
    if (item._type_id == LMD_TYPE_INT) {
        int64_t val = item.get_int56();
        return (Item) { .item = i2it(val < 0 ? -val : val) };
    }
    else if (item._type_id == LMD_TYPE_INT64) {
        int64_t val = item.get_int64();
        return push_l(val < 0 ? -val : val);
    }
    else if (item._type_id == LMD_TYPE_FLOAT) {
        double val = item.get_double();
        return push_d(fabs(val));
    }
    else {
        log_error("abs not supported for type: %d", item._type_id);
        return ItemError;
    }
}

Item fn_round(Item item) {
    // round() - round to nearest integer
    if (item._type_id == LMD_TYPE_INT || item._type_id == LMD_TYPE_INT64) {
        // Already an integer, return as-is
        return item;
    }
    else if (item._type_id == LMD_TYPE_FLOAT) {
        double val = item.get_double();
        return push_d(round(val));
    }
    else {
        log_debug("round not supported for type: %d", item._type_id);
        return ItemError;
    }
}

Item fn_floor(Item item) {
    // floor() - round down to nearest integer
    if (item._type_id == LMD_TYPE_INT || item._type_id == LMD_TYPE_INT64) {
        return item;  // return as-is
    }
    else if (item._type_id == LMD_TYPE_FLOAT) {
        double val = item.get_double();
        return push_d(floor(val));
    }
    else {
        log_debug("floor not supported for type: %d", item._type_id);
        return ItemError;
    }
}

Item fn_ceil(Item item) {
    // ceil() - round up to nearest integer
    if (item._type_id == LMD_TYPE_INT || item._type_id == LMD_TYPE_INT64) {
        return item;  // return as-is
    }
    else if (item._type_id == LMD_TYPE_FLOAT) {
        double val = item.get_double();
        return push_d(ceil(val));
    }
    else {
        log_debug("ceil not supported for type: %d", item._type_id);
        return ItemError;
    }
}

Item fn_min2(Item item_a, Item item_b) {
    log_debug("fn_min called with types: %d, %d", item_a._type_id, item_b._type_id);
    // two argument scalar min case
    double a_val = 0.0, b_val = 0.0;
    bool is_float = false;

    // Convert first argument
    if (item_a._type_id == LMD_TYPE_INT) {
        a_val = (double)item_a.get_int56();
    }
    else if (item_a._type_id == LMD_TYPE_INT64) {
        a_val = (double)item_a.get_int64();
    }
    else if (item_a._type_id == LMD_TYPE_FLOAT) {
        a_val = item_a.get_double();
        is_float = true;
    }
    else if (item_a._type_id == LMD_TYPE_DECIMAL) {
        log_error("decimal not supported yet in fn_min");
        return ItemError;
    }
    else {
        log_debug("min not supported for type: %d", item_a._type_id);
        return ItemError;
    }

    // convert second argument
    if (item_b._type_id == LMD_TYPE_INT) {
        b_val = (double)item_b.get_int56();
    }
    else if (item_b._type_id == LMD_TYPE_INT64) {
        b_val = (double)item_b.get_int64();
    }
    else if (item_b._type_id == LMD_TYPE_FLOAT) {
        b_val = item_b.get_double();
        is_float = true;
    }
    else if (item_a._type_id == LMD_TYPE_DECIMAL) {
        log_error("decimal not supported yet in fn_min");
        return ItemError;
    }
    else {
        log_debug("min not supported for type: %d", item_b._type_id);
        return ItemError;
    }

    double result = a_val < b_val ? a_val : b_val;
    // Return as integer if both inputs were integers
    if (!is_float) {
        // Convert back to Item int directly to match input type
        return { .item = i2it((int64_t)result) };
    }
    else {
        return push_d(result);
    }
}

Item fn_min1(Item item_a) {
    // single argument min case
    TypeId type_id = get_type_id(item_a);
    if (type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr = item_a.array_int;
        if (arr->length == 0) {
            return ItemError; // Empty array has no minimum
        }
        int32_t min_val = arr->items[0];
        for (size_t i = 1; i < arr->length; i++) {
            if (arr->items[i] < min_val) {
                min_val = arr->items[i];
            }
        }
        return { .item = i2it(min_val) };
    }
    else if (type_id == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr = item_a.array_int64;
        if (arr->length == 0) {
            return ItemError; // Empty array has no minimum
        }
        int64_t min_val = arr->items[0];
        for (size_t i = 1; i < arr->length; i++) {
            if (arr->items[i] < min_val) {
                min_val = arr->items[i];
            }
        }
        log_debug("min value (int64): %ld", min_val);
        return push_l(min_val);
    }
    else if (type_id == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr = item_a.array_float;
        if (arr->length == 0) {
            return ItemError; // Empty array has no minimum
        }
        double min_val = arr->items[0];
        for (size_t i = 1; i < arr->length; i++) {
            if (arr->items[i] < min_val) {
                min_val = arr->items[i];
            }
        }
        return push_d(min_val);
    }
    else if (type_id == LMD_TYPE_ARRAY || type_id == LMD_TYPE_LIST) {
        List* arr = item_a.list;
        if (!arr || arr->length == 0) {
            return ItemError; // Empty array has no minimum
        }
        Item min_item = type_id == LMD_TYPE_LIST ? list_get(arr, 0) : array_get(arr, 0);
        double min_val = 0.0;
        bool is_float = false;

        // Convert first element
        if (min_item._type_id == LMD_TYPE_INT) {
            min_val = (double)min_item.get_int56();
        }
        else if (min_item._type_id == LMD_TYPE_INT64) {
            min_val = (double)min_item.get_int64();
        }
        else if (min_item._type_id == LMD_TYPE_FLOAT) {
            min_val = min_item.get_double();
            is_float = true;
        }
        else if (min_item._type_id == LMD_TYPE_DECIMAL) {
            log_error("decimal not supported yet in fn_min");
            return ItemError;
        }
        else {
            log_error("non-numeric array element type: %d", min_item._type_id);
            return ItemError;
        }

        // find minimum
        for (size_t i = 1; i < arr->length; i++) {
            Item elem_item = type_id == LMD_TYPE_LIST ? list_get(arr, i) : array_get(arr, i);
            double elem_val = 0.0;

            if (elem_item._type_id == LMD_TYPE_INT) {
                elem_val = (double)elem_item.get_int56();
            }
            else if (elem_item._type_id == LMD_TYPE_INT64) {
                elem_val = (double)elem_item.get_int64();
            }
            else if (elem_item._type_id == LMD_TYPE_FLOAT) {
                elem_val = elem_item.get_double();
                is_float = true;
            }
            else if (elem_item._type_id == LMD_TYPE_DECIMAL) {
                log_error("decimal not supported yet in fn_min");
                return ItemError;
            }
            else {
                return ItemError;
            }
            if (elem_val < min_val) {
                min_val = elem_val;
            }
        }
        if (is_float) {
            return push_d(min_val);
        }
        else {
            return (Item) { .item = i2it((int64_t)min_val) };
        }
    }
    else if (LMD_TYPE_INT <= type_id && type_id <= LMD_TYPE_NUMBER) {
        // single numeric value, return as-is
        return item_a;
    }
    else {
        log_debug("min not supported for single argument type: %d", type_id);
        return ItemError;
    }
}

Item fn_max2(Item item_a, Item item_b) {
    // two argument max case
    double a_val = 0.0, b_val = 0.0;
    bool is_float = false;

    // Convert first argument
    if (item_a._type_id == LMD_TYPE_INT) {
        a_val = (double)item_a.get_int56();
    }
    else if (item_a._type_id == LMD_TYPE_INT64) {
        a_val = (double)item_a.get_int64();
    }
    else if (item_a._type_id == LMD_TYPE_FLOAT) {
        a_val = item_a.get_double();
        is_float = true;
    }
    else {
        log_debug("max not supported for type: %d", item_a._type_id);
        return ItemError;
    }

    // Convert second argument
    if (item_b._type_id == LMD_TYPE_INT) {
        b_val = (double)item_b.get_int56();
    }
    else if (item_b._type_id == LMD_TYPE_INT64) {
        b_val = (double)item_b.get_int64();
    }
    else if (item_b._type_id == LMD_TYPE_FLOAT) {
        b_val = item_b.get_double();
        is_float = true;
    }
    else {
        log_debug("max not supported for type: %d", item_b._type_id);
        return ItemError;
    }

    double result = a_val > b_val ? a_val : b_val;
    // Return as integer if both inputs were integers
    if (!is_float) {
        // Convert back to Item int directly to match input type
        return { .item = i2it((int64_t)result) };
    }
    else {
        return push_d(result);
    }
}

Item fn_max1(Item item_a) {
    // single argument max case
    TypeId type_id = get_type_id(item_a);
    if (type_id == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr = item_a.array_float;
        if (arr->length == 0) {
            return ItemError; // Empty array has no maximum
        }
        double max_val = arr->items[0];
        for (size_t i = 1; i < arr->length; i++) {
            if (arr->items[i] > max_val) {
                max_val = arr->items[i];
            }
        }
        return push_d(max_val);
    }
    else if (type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr = item_a.array_int;
        if (arr->length == 0) {
            return ItemError; // Empty array has no maximum
        }
        int32_t max_val = arr->items[0];
        for (size_t i = 1; i < arr->length; i++) {
            if (arr->items[i] > max_val) {
                max_val = arr->items[i];
            }
        }
        return { .item = i2it(max_val) };
    }
    else if (type_id == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr = item_a.array_int64;
        if (arr->length == 0) {
            return ItemError; // Empty array has no maximum
        }
        int64_t max_val = arr->items[0];
        for (size_t i = 1; i < arr->length; i++) {
            if (arr->items[i] > max_val) {
                max_val = arr->items[i];
            }
        }
        log_debug("max value (int64): %ld", max_val);
        return push_l(max_val);
    }
    else if (type_id == LMD_TYPE_ARRAY || type_id == LMD_TYPE_LIST) {
        Array* arr = item_a.array;
        if (!arr || arr->length == 0) {
            return ItemError; // Empty array has no maximum
        }
        Item max_item = type_id == LMD_TYPE_LIST ? list_get(arr, 0) : array_get(arr, 0);
        double max_val = 0.0;
        bool is_float = false;

        // Convert first element
        if (max_item._type_id == LMD_TYPE_INT) {
            max_val = (double)max_item.get_int56();
        }
        else if (max_item._type_id == LMD_TYPE_INT64) {
            max_val = (double)max_item.get_int64();
        }
        else if (max_item._type_id == LMD_TYPE_FLOAT) {
            max_val = max_item.get_double();
            is_float = true;
        }
        else {
            return ItemError;
        }

        // Find maximum
        for (size_t i = 1; i < arr->length; i++) {
            Item elem_item = type_id == LMD_TYPE_LIST ? list_get(arr, i) : array_get(arr, i);
            double elem_val = 0.0;

            if (elem_item._type_id == LMD_TYPE_INT) {
                elem_val = (double)elem_item.get_int56();
            }
            else if (elem_item._type_id == LMD_TYPE_INT64) {
                elem_val = (double)elem_item.get_int64();
            }
            else if (elem_item._type_id == LMD_TYPE_FLOAT) {
                elem_val = elem_item.get_double();
                is_float = true;
            }
            else {
                return ItemError;
            }

            if (elem_val > max_val) {
                max_val = elem_val;
            }
        }
        if (is_float) {
            return push_d(max_val);
        }
        else {
            return (Item) { .item = i2it((int64_t)max_val) };
        }
    }
    else if (LMD_TYPE_INT <= type_id && type_id <= LMD_TYPE_NUMBER) {
        // single numeric value, return as-is
        return item_a;
    }
    else {
        log_debug("max not supported for single argument type: %d", type_id);
        return ItemError;
    }
}

Item fn_sum(Item item) {
    // sum() - sum of all elements in an array or list
    TypeId type_id = get_type_id(item);
    log_debug("DEBUG fn_sum: called with type_id: %d", type_id);
    if (type_id == LMD_TYPE_ARRAY) {
        log_debug("DEBUG fn_sum: Processing LMD_TYPE_ARRAY");
        Array* arr = item.array;  // Use item.array, not item.pointer
        log_debug("DEBUG fn_sum: Array pointer: %p, length: %ld", arr, arr ? arr->length : -1);
        if (!arr || arr->length == 0) {
            log_debug("DEBUG fn_sum: Empty array, returning 0");
            return (Item) { .item = i2it(0) };  // Empty array sums to 0
        }
        double sum = 0.0;
        bool is_float = false;
        for (size_t i = 0; i < arr->length; i++) {
            Item elem_item = array_get(arr, i);
            if (elem_item._type_id == LMD_TYPE_INT) {
                int64_t val = elem_item.get_int56();
                log_debug("DEBUG fn_sum: Adding int value: %ld", val);
                sum += (double)val;
                is_float = true;
                // todo: keep as int if within range
            }
            else if (elem_item._type_id == LMD_TYPE_INT64) {
                int64_t val = elem_item.get_int64();
                log_debug("DEBUG fn_sum: Adding int64 value: %ld", val);
                sum += (double)val;
            }
            else if (elem_item._type_id == LMD_TYPE_FLOAT) {
                double val = elem_item.get_double();
                log_error("DEBUG fn_sum: Adding float value: %f", val);
                sum += val;
                is_float = true;
            }
            else {
                log_debug("DEBUG fn_sum: sum: non-numeric element at index %zu, type: %d", i, elem_item._type_id);
                return ItemError;
            }
        }
        if (is_float) {
            return push_d(sum);
        }
        else {
            return push_l((int64_t)sum);
        }
    }
    else if (type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr = item.array_int;  // Use the correct field
        if (arr->length == 0) {
            return (Item) { .item = i2it(0) };  // Empty array sums to 0
        }
        int64_t sum = 0;
        for (size_t i = 0; i < arr->length; i++) {
            sum += arr->items[i];
        }
        return push_l(sum);
    }
    else if (type_id == LMD_TYPE_ARRAY_INT64) {
        log_debug("fn_sum of LMD_TYPE_ARRAY_INT64");
        ArrayInt64* arr = item.array_int64;
        if (arr->length == 0) {
            return (Item) { .item = i2it(0) };  // Empty array sums to 0
        }
        int64_t sum = 0;
        for (size_t i = 0; i < arr->length; i++) {
            sum += arr->items[i];
        }
        log_debug("fn_sum of LMD_TYPE_ARRAY_INT64: %ld", sum);
        return push_l(sum);
    }
    else if (type_id == LMD_TYPE_ARRAY_FLOAT) {
        log_debug("fn_sum of LMD_TYPE_ARRAY_FLOAT");
        ArrayFloat* arr = item.array_float;  // Use the correct field
        if (arr->length == 0) {
            return push_d(0.0);  // Empty array sums to 0.0
        }
        double sum = 0.0;
        for (size_t i = 0; i < arr->length; i++) {
            sum += arr->items[i];
        }
        log_debug("fn_sum result: %f", sum);
        return push_d(sum);
    }
    else if (type_id == LMD_TYPE_LIST) {
        log_debug("DEBUG fn_sum: Processing LMD_TYPE_LIST");
        List* list = item.list;
        log_debug("DEBUG fn_sum: List pointer: %p, length: %ld", list, list ? list->length : -1);
        if (!list || list->length == 0) {
            log_debug("DEBUG fn_sum: Empty list, returning 0");
            return (Item) { .item = i2it(0) };  // Empty list sums to 0
        }
        double sum = 0.0;
        bool has_float = false;
        for (size_t i = 0; i < list->length; i++) {
            Item elem_item = list_get(list, i);
            if (elem_item._type_id == LMD_TYPE_INT) {
                int64_t val = elem_item.get_int56();
                log_debug("DEBUG fn_sum: Adding int value: %ld", val);
                sum += (double)val;
            }
            else if (elem_item._type_id == LMD_TYPE_INT64) {
                int64_t val = elem_item.get_int64();
                log_debug("DEBUG fn_sum: Adding int64 value: %ld", val);
                sum += (double)val;
            }
            else if (elem_item._type_id == LMD_TYPE_FLOAT) {
                double val = elem_item.get_double();
                log_debug("DEBUG fn_sum: Adding float value: %f", val);
                sum += val;
                has_float = true;
            }
            else {
                log_debug("DEBUG fn_sum: sum: non-numeric element at index %zu, type: %d", i, elem_item._type_id);
                return ItemError;
            }
        }
        if (has_float) {
            log_debug("DEBUG fn_sum: Returning sum as double: %f", sum);
            return push_d(sum);
        }
        else {
            if (sum > INT_MAX || sum < INT_MIN) {
                log_debug("DEBUG fn_sum: Returning sum as long: %" PRId64, (int64_t)sum);
                return push_l(sum);
            } else{
                log_debug("DEBUG fn_sum: Returning sum as int: %d", (int32_t)sum);
                return {.item = i2it((int32_t)sum)};
            }
        }
    }
    else if (LMD_TYPE_INT <= type_id && type_id <= LMD_TYPE_NUMBER) {
        // single numeric value, return as-is
        return item;
    }
    else {
        log_debug("DEBUG fn_sum: sum not supported for type: %d", type_id);
        return ItemError;
    }
}

Item fn_avg(Item item) {
    // avg() - average of all elements in an array or list
    TypeId type_id = get_type_id(item);
    if (type_id == LMD_TYPE_ARRAY) {
        Array* arr = item.array;  // Use item.array, not item.pointer
        if (!arr || arr->length == 0) {
            return ItemError;
        }
        double sum = 0.0;
        for (size_t i = 0; i < arr->length; i++) {
            Item elem_item = array_get(arr, i);
            if (elem_item._type_id == LMD_TYPE_INT) {
                int64_t val = elem_item.get_int56();
                sum += (double)val;
            }
            else if (elem_item._type_id == LMD_TYPE_INT64) {
                sum += (double)elem_item.get_int64();
            }
            else if (elem_item._type_id == LMD_TYPE_FLOAT) {
                sum += elem_item.get_double();
            }
            else {
                log_debug("avg: non-numeric element at index %zu, type: %d", i, elem_item._type_id);
                return ItemError;
            }
        }
        return push_d(sum / (double)arr->length);
    }
    else if (type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr = item.array_int;  // Use the correct field
        if (!arr || arr->length == 0) {
            return ItemError;
        }
        double sum = 0.0;
        for (size_t i = 0; i < arr->length; i++) {
            sum += (double)arr->items[i];
        }
        return push_d(sum / (double)arr->length);
    }
    else if (type_id == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr = item.array_int64;
        if (!arr || arr->length == 0) {
            return ItemError;
        }
        double sum = 0.0;
        for (size_t i = 0; i < arr->length; i++) {
            sum += (double)arr->items[i];
        }
        return push_d(sum / (double)arr->length);
    }
    else if (type_id == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr = item.array_float;  // Use the correct field
        if (!arr || arr->length == 0) {
            return ItemError;
        }
        double sum = 0.0;
        for (size_t i = 0; i < arr->length; i++) {
            sum += arr->items[i];
        }
        return push_d(sum / (double)arr->length);
    }
    else if (type_id == LMD_TYPE_LIST) {
        List* list = item.list;
        if (!list || list->length == 0) {
            return ItemError;
        }
        double sum = 0.0;
        for (size_t i = 0; i < list->length; i++) {
            Item elem_item = list_get(list, i);
            if (elem_item._type_id == LMD_TYPE_INT) {
                int64_t val = elem_item.get_int56();
                sum += (double)val;
            }
            else if (elem_item._type_id == LMD_TYPE_INT64) {
                sum += (double)elem_item.get_int64();
            }
            else if (elem_item._type_id == LMD_TYPE_FLOAT) {
                sum += elem_item.get_double();
            }
            else {
                log_debug("avg: non-numeric element at index %zu, type: %d", i, elem_item._type_id);
                return ItemError;
            }
        }
        return push_d(sum / (double)list->length);
    }
    else if (LMD_TYPE_INT <= type_id && type_id <= LMD_TYPE_NUMBER) {
        // single numeric value, return as-is
        return item;
    }
    else {
        log_debug("avg not supported for type: %d", type_id);
        return ItemError;
    }
}

Item fn_pos(Item item) {
    // Unary + operator - return the item as-is for numeric types, or cast strings/symbols to numbers
    if (item._type_id == LMD_TYPE_INT) {
        return item;  // Already in correct format
    }
    else if (item._type_id == LMD_TYPE_INT64) {
        return item;  // Already in correct format
    }
    else if (item._type_id == LMD_TYPE_FLOAT) {
        return item;  // Already in correct format
    }
    else if (item._type_id == LMD_TYPE_DECIMAL) {
        return item;  // For decimal, unary + returns the same value
    }
    else if (item._type_id == LMD_TYPE_STRING || item._type_id == LMD_TYPE_SYMBOL) {
        // Cast string/symbol to number
        String* str = item.get_string();
        if (!str || str->len == 0) {
            log_error("unary + error: empty string/symbol");
            return ItemError;
        }

        // Try to parse as integer first
        char* endptr;
        int64_t long_val = strtol(str->chars, &endptr, 10);

        // If entire string was consumed and no overflow, it's an integer
        if (*endptr == '\0' && endptr == str->chars + str->len) {
            return (Item) { .item = i2it(long_val) };
        }

        // Try to parse as float
        double double_val = strtod(str->chars, &endptr);
        if (*endptr == '\0' && endptr == str->chars + str->len) {
            return push_d(double_val);
        }

        // Not a valid number
        log_error("unary + error: cannot convert '%.*s' to number", (int)str->len, str->chars);
        return ItemError;
    }
    else {
        // For other types (bool, datetime, etc.), unary + is an error
        log_debug("unary + not supported for type: %d", item._type_id);
        return ItemError;
    }
}

Item fn_neg(Item item) {
    // Unary - operator - negate numeric values or cast and negate strings/symbols
    if (item._type_id == LMD_TYPE_INT) {
        int64_t val = item.get_int56();
        return (Item) { .item = i2it(-val) };
    }
    else if (item._type_id == LMD_TYPE_INT64) {
        int64_t val = item.get_int64();
        return push_l(-val);
    }
    else if (item._type_id == LMD_TYPE_FLOAT) {
        double val = item.get_double();
        return push_d(-val);
    }
    else if (item._type_id == LMD_TYPE_DECIMAL) {
        // For decimal types, we'd need to negate the libmpdec value
        // This would require more complex decimal arithmetic with libmpdec
        log_debug("unary - for decimal type not yet implemented");
        return ItemError;
    }
    else if (item._type_id == LMD_TYPE_STRING || item._type_id == LMD_TYPE_SYMBOL) {
        // Cast string/symbol to number, then negate
        String* str = item.get_string();
        if (!str || str->len == 0) {
            log_debug("unary - error: empty string/symbol");
            return ItemError;
        }

        // Try to parse as integer first
        char* endptr;
        int64_t long_val = strtol(str->chars, &endptr, 10);

        // If entire string was consumed and no overflow, it's an integer
        if (*endptr == '\0' && endptr == str->chars + str->len) {
            return (Item) { .item = i2it(-long_val) };
        }

        // Try to parse as float
        double double_val = strtod(str->chars, &endptr);
        if (*endptr == '\0' && endptr == str->chars + str->len) {
            return push_d(-double_val);
        }

        // Not a valid number
        log_debug("unary - error: cannot convert '%.*s' to number", (int)str->len, str->chars);
        return ItemError;
    }
    else {
        // For other types (bool, datetime, etc.), unary - is an error
        log_debug("unary - not supported for type: %d", item._type_id);
        return ItemError;
    }
}

Item fn_int(Item item) {
    double dval;  int64_t ival;
    if (item._type_id == LMD_TYPE_INT) {
        return item;
    }
    else if (item._type_id == LMD_TYPE_INT64) {
        dval = item.get_int64();
        goto CHECK_DVAL;
    }
    else if (item._type_id == LMD_TYPE_FLOAT) {
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
    else if (item._type_id == LMD_TYPE_DECIMAL) {
        // todo: truncate any fractional part
        return item;  // keep it as it is
    }
    else if (item._type_id == LMD_TYPE_STRING || item._type_id == LMD_TYPE_SYMBOL) {
        String* str = item.get_string();
        if (!str || str->len == 0) {
            return ItemError;
        }
        char* endptr;
        // try to parse as int32 first
        errno = 0;  // clear errno before calling strtoll
        int32_t val = strtol(str->chars, &endptr, 10);
        if (endptr == str->chars) {
            log_debug("Cannot convert string '%s' to int", str->chars);
            return ItemError;
        }
        // check for overflow - if errno is set or we couldn't parse the full string
        if (errno == ERANGE || (*endptr != '\0')) {
            // try to parse as decimal
            mpd_t* dec_val = mpd_new(context->decimal_ctx);
            if (!dec_val) {
                log_debug("Failed to allocate decimal for string conversion");
                return ItemError;
            }
            mpd_set_string(dec_val, str->chars, context->decimal_ctx);
            if (mpd_isnan(dec_val) || mpd_isinfinite(dec_val)) {
                log_debug("Cannot convert string '%s' to decimal", str->chars);
                mpd_del(dec_val);
                return ItemError;
            }
            log_debug("promote string to decimal: %s", str->chars);
            return push_decimal(dec_val);
        }
        return (Item) { .item = i2it(val) };
    }
    else {
        log_debug("Cannot convert type %d to int", item._type_id);
        return ItemError;
    }
}

int64_t fn_int64(Item item) {
    // convert item to int64
    int64_t val;
    if (item._type_id == LMD_TYPE_INT) {
        log_debug("convert int to int64: %lld", item.get_int56());
        return item.get_int56();
    }
    else if (item._type_id == LMD_TYPE_INT64) {
        return item.get_int64();
    }
    else if (item._type_id == LMD_TYPE_FLOAT) {
        double dval = item.get_double();
        double truncated = (double)(int64_t)dval;
        if (truncated > LAMBDA_INT64_MAX || truncated < INT64_MIN) {
            log_debug("float value %g out of int64 range", dval);
            return INT64_ERROR;
        }
        return truncated;
    }
    else if (item._type_id == LMD_TYPE_DECIMAL) {
        // Convert decimal to int64
        Decimal* dec_ptr = item.get_decimal();
        mpd_t* dec = dec_ptr->dec_val;
        if (!dec) {
            log_debug("decimal pointer is NULL");
            return INT64_ERROR;
        }
        char* endptr;
        char* dec_str = mpd_to_sci(dec, 1);
        if (!dec_str) {
            log_debug("mpd_to_sci failed");
            return INT64_ERROR;
        }
        log_debug("convert decimal to int64: %s", dec_str);
        val = strtoll(dec_str, &endptr, 10);
        mpd_free(dec_str);
        if (endptr == dec_str) {
            log_debug("Cannot convert decimal to int64");
            return INT64_ERROR;
        }
        return val;
    }
    else if (item._type_id == LMD_TYPE_STRING || item._type_id == LMD_TYPE_SYMBOL) {
        String* str = item.get_string();
        if (!str || str->len == 0) {
            return 0;
        }
        char* endptr;
        log_debug("convert string/symbol to int64: %s", str->chars);
        errno = 0;  // clear errno before calling strtoll
        int64_t val = strtoll(str->chars, &endptr, 10);
        if (endptr == str->chars) {
            log_debug("Cannot convert string '%s' to int64", str->chars);
            return INT64_ERROR;
        }
        if (errno == ERANGE) {
            log_debug("String value '%s' out of int64 range", str->chars);
            return INT64_ERROR;
        }
        log_debug("converted string to int64: %" PRId64, val);
        return val;
    }
    log_debug("Cannot convert type %d to int64", item._type_id);
    return INT64_ERROR;
}

// Constructor functions for type conversion

Item fn_decimal(Item item) {
    // Convert item to decimal type
    if (item._type_id == LMD_TYPE_DECIMAL) {
        return item;  // Already a decimal
    }
    else if (item._type_id == LMD_TYPE_INT) {
        mpd_t* dec_val = mpd_new(context->decimal_ctx);
        if (!dec_val) {
            log_debug("Failed to allocate decimal");
            return ItemError;
        }
        mpd_set_ssize(dec_val, item.get_int56(), context->decimal_ctx);
        return push_decimal(dec_val);
    }
    else if (item._type_id == LMD_TYPE_INT64) {
        mpd_t* dec_val = mpd_new(context->decimal_ctx);
        if (!dec_val) {
            log_debug("Failed to allocate decimal");
            return ItemError;
        }
        int64_t val = item.get_int64();
        mpd_set_i64(dec_val, val, context->decimal_ctx);
        return push_decimal(dec_val);
    }
    else if (item._type_id == LMD_TYPE_FLOAT) {
        mpd_t* dec_val = mpd_new(context->decimal_ctx);
        if (!dec_val) {
            log_debug("Failed to allocate decimal");
            return ItemError;
        }
        double val = item.get_double();
        char str_buf[64];
        snprintf(str_buf, sizeof(str_buf), "%.17g", val);
        mpd_set_string(dec_val, str_buf, context->decimal_ctx);
        return push_decimal(dec_val);
    }
    else if (item._type_id == LMD_TYPE_STRING || item._type_id == LMD_TYPE_SYMBOL) {
        String* str = item.get_string();
        if (!str || str->len == 0) {
            log_debug("Cannot convert empty string/symbol to decimal");
            return ItemError;
        }

        mpd_t* dec_val = mpd_new(context->decimal_ctx);
        if (!dec_val) {
            log_debug("Failed to allocate decimal");
            return ItemError;
        }

        // Create null-terminated string for mpd_set_string
        char* null_term_str = (char*)malloc(str->len + 1);
        if (!null_term_str) {
            mpd_del(dec_val);
            log_debug("Failed to allocate string buffer");
            return ItemError;
        }
        memcpy(null_term_str, str->chars, str->len);
        null_term_str[str->len] = '\0';

        mpd_set_string(dec_val, null_term_str, context->decimal_ctx);
        free(null_term_str);

        if (mpd_isnan(dec_val) || mpd_isinfinite(dec_val)) {
            log_debug("Cannot convert string '%.*s' to decimal", (int)str->len, str->chars);
            mpd_del(dec_val);
            return ItemError;
        }

        return push_decimal(dec_val);
    }
    else {
        log_debug("Cannot convert type %d to decimal", item._type_id);
        return ItemError;
    }
}

Item fn_binary(Item item) {
    // Convert item to binary (string) type
    if (item._type_id == LMD_TYPE_STRING) {
        return item;  // Already a string (binary is stored as string)
    }
    else if (item._type_id == LMD_TYPE_SYMBOL) {
        // Convert symbol to string
        String* sym = item.get_string();
        if (!sym) {
            log_debug("Cannot convert null symbol to binary");
            return ItemError;
        }

        String* str = (String*)heap_alloc(sizeof(String) + sym->len + 1, LMD_TYPE_STRING);
        if (!str) {
            log_debug("Failed to allocate string for binary conversion");
            return ItemError;
        }

        str->len = sym->len;
        memcpy(str->chars, sym->chars, sym->len);
        str->chars[sym->len] = '\0';

        return (Item) { .item = s2it(str) };
    }
    else if (item._type_id == LMD_TYPE_INT) {
        // Convert int to binary string representation
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%lld", (long long)item.get_int56());
        if (len < 0 || len >= (int)sizeof(buf)) {
            log_debug("Failed to convert int to string");
            return ItemError;
        }

        String* str = (String*)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_STRING);
        if (!str) {
            log_debug("Failed to allocate string for binary conversion");
            return ItemError;
        }

        str->len = len;
        memcpy(str->chars, buf, len);
        str->chars[len] = '\0';

        return (Item) { .item = s2it(str) };
    }
    else if (item._type_id == LMD_TYPE_INT64) {
        // Convert int64 to binary string representation
        int64_t val = item.get_int64();
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%" PRId64, val);
        if (len < 0 || len >= (int)sizeof(buf)) {
            log_debug("Failed to convert int64 to string");
            return ItemError;
        }

        String* str = (String*)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_STRING);
        if (!str) {
            log_debug("Failed to allocate string for binary conversion");
            return ItemError;
        }

        str->len = len;
        memcpy(str->chars, buf, len);
        str->chars[len] = '\0';

        return (Item) { .item = s2it(str) };
    }
    else if (item._type_id == LMD_TYPE_FLOAT) {
        // Convert float to binary string representation
        double val = item.get_double();
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "%.17g", val);
        if (len < 0 || len >= (int)sizeof(buf)) {
            log_debug("Failed to convert float to string");
            return ItemError;
        }

        String* str = (String*)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_STRING);
        if (!str) {
            log_debug("Failed to allocate string for binary conversion");
            return ItemError;
        }

        str->len = len;
        memcpy(str->chars, buf, len);
        str->chars[len] = '\0';

        return (Item) { .item = s2it(str) };
    }
    else {
        log_debug("Cannot convert type %d to binary", item._type_id);
        return ItemError;
    }
}

Item fn_symbol(Item item) {
    // Convert item to symbol type
    if (item._type_id == LMD_TYPE_SYMBOL) {
        return item;  // Already a symbol
    }
    else if (item._type_id == LMD_TYPE_STRING) {
        // Convert string to symbol
        String* str = item.get_string();
        if (!str) {
            log_debug("Cannot convert null string to symbol");
            return ItemError;
        }

        String* sym = (String*)heap_alloc(sizeof(String) + str->len + 1, LMD_TYPE_SYMBOL);
        if (!sym) {
            log_debug("Failed to allocate symbol");
            return ItemError;
        }

        sym->len = str->len;
        memcpy(sym->chars, str->chars, str->len);
        sym->chars[sym->len] = '\0';

        return (Item) { .item = y2it(sym) };
    }
    else if (item._type_id == LMD_TYPE_INT) {
        // Convert int to symbol
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%lld", (long long)item.get_int56());
        if (len < 0 || len >= (int)sizeof(buf)) {
            log_debug("Failed to convert int to symbol");
            return ItemError;
        }

        String* sym = (String*)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_SYMBOL);
        if (!sym) {
            log_debug("Failed to allocate symbol");
            return ItemError;
        }

        sym->len = len;
        memcpy(sym->chars, buf, len);
        sym->chars[len] = '\0';

        return (Item) { .item = y2it(sym) };
    }
    else if (item._type_id == LMD_TYPE_INT64) {
        // Convert int64 to symbol
        int64_t val = item.get_int64();
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%" PRId64, val);
        if (len < 0 || len >= (int)sizeof(buf)) {
            log_debug("Failed to convert int64 to symbol");
            return ItemError;
        }

        String* sym = (String*)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_SYMBOL);
        if (!sym) {
            log_debug("Failed to allocate symbol");
            return ItemError;
        }

        sym->len = len;
        memcpy(sym->chars, buf, len);
        sym->chars[len] = '\0';

        return (Item) { .item = y2it(sym) };
    }
    else if (item._type_id == LMD_TYPE_FLOAT) {
        // Convert float to symbol
        double val = item.get_double();
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "%.17g", val);
        if (len < 0 || len >= (int)sizeof(buf)) {
            log_debug("Failed to convert float to symbol");
            return ItemError;
        }

        String* sym = (String*)heap_alloc(sizeof(String) + len + 1, LMD_TYPE_SYMBOL);
        if (!sym) {
            log_debug("Failed to allocate symbol");
            return ItemError;
        }

        sym->len = len;
        memcpy(sym->chars, buf, len);
        sym->chars[len] = '\0';

        return (Item) { .item = y2it(sym) };
    }
    else {
        log_debug("Cannot convert type %d to symbol", item._type_id);
        return ItemError;
    }
}

Item fn_float(Item item) {
    // Convert item to float type
    if (item._type_id == LMD_TYPE_FLOAT) {
        return item;  // Already a float
    }
    else if (item._type_id == LMD_TYPE_INT) {
        double* val = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        if (!val) {
            log_debug("Failed to allocate float");
            return ItemError;
        }
        *val = (double)item.get_int56();

        return (Item) { .item = d2it(val) };
    }
    else if (item._type_id == LMD_TYPE_INT64) {
        int64_t int_val = item.get_int64();
        double* val = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        if (!val) {
            log_debug("Failed to allocate float");
            return ItemError;
        }
        *val = (double)int_val;

        return (Item) { .item = d2it(val) };
    }
    else if (item._type_id == LMD_TYPE_DECIMAL) {
        Decimal* dec_ptr = item.get_decimal();
        mpd_t* dec_val = dec_ptr->dec_val;
        char* dec_str = mpd_to_sci(dec_val, 1);
        if (!dec_str) {
            log_debug("Failed to convert decimal to string");
            return ItemError;
        }
        char* endptr;
        errno = 0;
        double dval = strtod(dec_str, &endptr);
        mpd_free(dec_str);

        if (errno != 0 || *endptr != '\0') {
            log_debug("Failed to convert decimal to float");
            return ItemError;
        }

        double* val = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        if (!val) {
            log_debug("Failed to allocate float");
            return ItemError;
        }
        *val = dval;

        return (Item) { .item = d2it(val) };
    }
    else if (item._type_id == LMD_TYPE_STRING || item._type_id == LMD_TYPE_SYMBOL) {
        String* str = item.get_string();
        if (!str || str->len == 0) {
            log_debug("Empty string/symbol cannot be converted to float");
            return ItemError;
        }

        // Create a null-terminated copy of the string
        char* buf = (char*)malloc(str->len + 1);
        if (!buf) {
            log_debug("Failed to allocate buffer for string conversion");
            return ItemError;
        }
        memcpy(buf, str->chars, str->len);
        buf[str->len] = '\0';

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
        free(buf);

        if (errno != 0 || *endptr != '\0') {
            log_debug("Cannot convert string to float: %.*s", (int)str->len, str->chars);
            return ItemError;
        }

        double* result_val = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        if (!result_val) {
            log_debug("Failed to allocate float");
            return ItemError;
        }
        *result_val = val;

        return (Item) { .item = d2it(result_val) };
    }
    else {
        log_debug("Cannot convert type %d to float", item._type_id);
        return ItemError;
    }
}
