#include "transpiler.hpp"

// Always enable Unicode support with utf8proc
#define LAMBDA_UNICODE_LEVEL 2
#define LAMBDA_UNICODE_UTF8PROC 2
#define LAMBDA_UNICODE_COMPACT 1
#include "utf_string.h"

#include <stdarg.h>
#include <time.h>
#include <cstdlib>  // for abs function
#include <cmath>    // for pow function
#include <errno.h>  // for errno checking

extern __thread Context* context;

#define stack_alloc(size) alloca(size);

#define Malloc malloc
#define Realloc realloc

Item _map_get(TypeMap* map_type, void* map_data, char *key, bool *is_found);

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

Item push_c(long cval) {
    if (cval == INT_ERROR) { return ItemError; }
    mpd_t* result = mpd_new(context->decimal_ctx);
    if (!result) return ItemError;
    mpd_set_ssize(result, cval, context->decimal_ctx);
    return push_decimal(result);
}

mpd_t* convert_to_decimal(Item item, mpd_context_t* ctx) {
    if (item.type_id == LMD_TYPE_DECIMAL) {
        Decimal* dec_ptr = (Decimal*)item.pointer;
        return dec_ptr->dec_val;
    }
    
    mpd_t* result = mpd_new(ctx);
    if (!result) return NULL;
    
    if (item.type_id == LMD_TYPE_INT) {
        mpd_set_ssize(result, item.int_val, ctx);
    } else if (item.type_id == LMD_TYPE_INT64) {
        long val = *(long*)item.pointer;
        mpd_set_ssize(result, val, ctx);
    } else if (item.type_id == LMD_TYPE_FLOAT) {
        double val = *(double*)item.pointer;
        char str_buf[64];
        snprintf(str_buf, sizeof(str_buf), "%.17g", val);
        mpd_set_string(result, str_buf, ctx);
    } else {
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

ArrayInt* array_int() {
    ArrayInt *arr = (ArrayInt*)heap_calloc(sizeof(ArrayInt), LMD_TYPE_ARRAY_INT);
    arr->type_id = LMD_TYPE_ARRAY_INT;
    return arr;
}

ArrayInt* array_int_new(int length) {
    ArrayInt *arr = (ArrayInt*)heap_calloc(sizeof(ArrayInt), LMD_TYPE_ARRAY_INT);
    arr->type_id = LMD_TYPE_ARRAY_INT;
    arr->length = length;  arr->capacity = length;
    arr->items = (int32_t*)Malloc(length * sizeof(int32_t));
    return arr;
}

ArrayInt* array_int_fill(ArrayInt *arr, int count, ...) {
    if (count < 0) { return NULL; }
    va_list args;
    va_start(args, count);
    arr->items = (int32_t*)Malloc(count * sizeof(int32_t));
    arr->length = count;  arr->capacity = count;
    for (int i = 0; i < count; i++) {
        arr->items[i] = va_arg(args, int32_t);
    }       
    va_end(args);
    return arr;
}

ArrayInt64* array_int64() {
    ArrayInt64 *arr = (ArrayInt64*)heap_calloc(sizeof(ArrayInt64), LMD_TYPE_ARRAY_INT64);
    arr->type_id = LMD_TYPE_ARRAY_INT64;
    return arr;
}

ArrayInt64* array_int64_new(int length) {
    ArrayInt64 *arr = (ArrayInt64*)heap_calloc(sizeof(ArrayInt64), LMD_TYPE_ARRAY_INT64);
    arr->type_id = LMD_TYPE_ARRAY_INT64;
    arr->length = length;  arr->capacity = length;
    arr->items = (int64_t*)Malloc(length * sizeof(int64_t));
    return arr;
}

ArrayInt64* array_int64_fill(ArrayInt64 *arr, int count, ...) {
    if (count < 0) { return NULL; }
    va_list args;
    va_start(args, count);
    arr->items = (int64_t*)Malloc(count * sizeof(int64_t));
    arr->length = count;  arr->capacity = count;
    for (int i = 0; i < count; i++) {
        arr->items[i] = va_arg(args, int64_t);
    }       
    va_end(args);
    printf("returning array_int64_fill: %d\n", arr->type_id);
    return arr;
}

Item array_int64_get_item(ArrayInt64* array, int index) {
    if (!array || index < 0 || index >= array->length) {
        return ItemNull;
    }
    return push_l(array->items[index]);
}

ArrayFloat* array_float() {
    ArrayFloat *arr = (ArrayFloat*)heap_calloc(sizeof(ArrayFloat), LMD_TYPE_ARRAY_FLOAT);
    arr->type_id = LMD_TYPE_ARRAY_FLOAT;
    return arr;
}

ArrayFloat* array_float_new(int length) {
    ArrayFloat *arr = (ArrayFloat*)heap_calloc(sizeof(ArrayFloat), LMD_TYPE_ARRAY_FLOAT);
    arr->type_id = LMD_TYPE_ARRAY_FLOAT;
    arr->length = length;  arr->capacity = length;
    arr->items = (double*)Malloc(length * sizeof(double));
    return arr;
}

ArrayFloat* array_float_fill(ArrayFloat *arr, int count, ...) {
    if (count < 0) { return NULL; } 
    va_list args;
    va_start(args, count);
    arr->type_id = LMD_TYPE_ARRAY_FLOAT;  
    arr->items = (double*)Malloc(count * sizeof(double));
    arr->length = count;  arr->capacity = count;
    for (int i = 0; i < count; i++) {
        arr->items[i] = va_arg(args, double);
    }       
    va_end(args);
    return arr;
}

bool item_true(Item item) {
    switch (item.type_id) {
    case LMD_TYPE_NULL:
        return false;
    case LMD_TYPE_ERROR:
        return false;
    case LMD_TYPE_BOOL:
        return item.bool_val;
    default:
        return true;
    }
}

// Convert Item to boolean Item, preserving errors
Item safe_b2it(Item item) {
    switch (item.type_id) {
    case LMD_TYPE_ERROR:
        // Preserve error - don't convert to boolean
        return item;
    case LMD_TYPE_BOOL:
        // Already boolean, return as-is
        return item;
    case LMD_TYPE_NULL:
        return {.item = b2it(false)};
    default:
        // Convert to boolean based on truthiness
        return {.item = b2it(item_true(item))};
    }
}

// list to item
Item v2it(List* list) {
    if (!list) { return ItemNull; }
    printf("v2it %p, length: %ld\n", list, list->length);
    if (list->length == 0) { return ItemNull; }
    if (list->length == 1) { return list->items[0]; }
    return {.list = list};
}

String *fn_strcat(String *left, String *right) {
    printf("fn_strcat %p, %p\n", left, right);
    if (!left || !right) {
        printf("Error: null pointer in fn_strcat: left=%p, right=%p\n", left, right);
        return NULL;
    }
    int left_len = left->len, right_len = right->len;
    printf("left len %d, right len %d\n", left_len, right_len);
    String *result = (String *)heap_alloc(sizeof(String) + left_len + right_len + 1, LMD_TYPE_STRING);
    if (!result) {
        printf("Error: failed to allocate memory for fn_strcat result\n");
        return NULL;
    }
    printf("str result %p\n", result);
    result->ref_cnt = 0;  result->len = left_len + right_len;
    memcpy(result->chars, left->chars, left_len);
    // copy the string and '\0'
    memcpy(result->chars + left_len, right->chars, right_len + 1);
    printf("fn_strcat result: %s\n", result->chars);
    return result;
}

Item fn_add(Item item_a, Item item_b) {
    // todo: join list, array, map
    if (item_a.type_id == LMD_TYPE_STRING && item_b.type_id == LMD_TYPE_STRING) {
        String *str_a = (String*)item_a.pointer;  String *str_b = (String*)item_b.pointer;
        String *result = fn_strcat(str_a, str_b);
        return {.item = s2it(result)};
    }
    else if (item_a.type_id == LMD_TYPE_SYMBOL && item_b.type_id == LMD_TYPE_SYMBOL) {
        String *str_a = (String*)item_a.pointer;  String *str_b = (String*)item_b.pointer;
        String *result = fn_strcat(str_a, str_b);
        return {.item = y2it(result)};
    }
    else if (item_a.type_id == LMD_TYPE_BINARY && item_b.type_id == LMD_TYPE_BINARY) {
        String *str_a = (String*)item_a.pointer;  String *str_b = (String*)item_b.pointer;
        String *result = fn_strcat(str_a, str_b);
        return {.item = x2it(result)};
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        return {.item = i2it(item_a.int_val + item_b.int_val)};
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT64) {
        return push_l(*(int64_t*)item_a.pointer + *(int64_t*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT64) {
        return push_l((int64_t)item_a.int_val + *(int64_t*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT) {
        return push_l(*(int64_t*)item_a.pointer + (int64_t)item_b.int_val);
    }
    // ArrayInt and ArrayInt64 support
    else if (item_a.type_id == LMD_TYPE_ARRAY_INT && item_b.type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr_a = item_a.array_int;
        ArrayInt* arr_b = item_b.array_int;
        if (arr_a->length != arr_b->length) {
            printf("Array length mismatch in addition\n");
            return ItemError;
        }
        ArrayInt* result = array_int_new(arr_a->length);
        for (long i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] + arr_b->items[i];
        }
        Item result_item;
        result_item.type_id = LMD_TYPE_ARRAY_INT;
        result_item.array_int = result;
        return result_item;
    }
    else if (item_a.type_id == LMD_TYPE_ARRAY_INT64 && item_b.type_id == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr_a = item_a.array_int64;
        ArrayInt64* arr_b = item_b.array_int64;
        if (arr_a->length != arr_b->length) {
            printf("Array length mismatch in addition\n");
            return ItemError;
        }
        ArrayInt64* result = array_int64_new(arr_a->length);
        for (long i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] + arr_b->items[i];
        }
        Item result_item;
        result_item.type_id = LMD_TYPE_ARRAY_INT64;
        result_item.array_int64 = result;
        return result_item;
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_FLOAT) {
        printf("add float: %g + %g\n", *(double*)item_a.pointer, *(double*)item_b.pointer);
        return push_d(*(double*)item_a.pointer + *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_FLOAT) {
        return push_d((double)item_a.int_val + *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_INT) {
        return push_d(*(double*)item_a.pointer + (double)item_b.int_val);
    }
    // Add libmpdec decimal support
    else if (item_a.type_id == LMD_TYPE_DECIMAL || item_b.type_id == LMD_TYPE_DECIMAL) {
        mpd_t* a_dec = convert_to_decimal(item_a, context->decimal_ctx);
        mpd_t* b_dec = convert_to_decimal(item_b, context->decimal_ctx);
        
        if (!a_dec || !b_dec) {
            if (a_dec) cleanup_temp_decimal(a_dec, item_a.type_id);
            if (b_dec) cleanup_temp_decimal(b_dec, item_b.type_id);
            printf("decimal conversion failed in fn_add\n");
            return ItemError;
        }
        
        mpd_t* result = mpd_new(context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }
        
        mpd_add(result, a_dec, b_dec, context->decimal_ctx);
        
        cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
        
        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            printf("decimal addition failed\n");
            return ItemError;
        }
        
        return push_decimal(result);
    }
    else {
        printf("unknown add type: %d, %d\n", item_a.type_id, item_b.type_id);
    }
    return ItemError;
}

String *str_repeat(String *str, long times) {
    if (times <= 0) {
        // Return empty string
        String *result = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
        result->ref_cnt = 0;
        result->len = 0;
        result->chars[0] = '\0';
        return result;
    }
    
    size_t str_len = str->len;
    size_t total_len = str_len * times;
    String *result = (String *)heap_alloc(sizeof(String) + total_len + 1, LMD_TYPE_STRING);
    result->ref_cnt = 0;
    result->len = total_len;
    
    for (long i = 0; i < times; i++) {
        memcpy(result->chars + (i * str_len), str->chars, str_len);
    }
    result->chars[total_len] = '\0';
    
    return result;
}

Item fn_mul(Item item_a, Item item_b) {
    if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        return {.item = i2it(item_a.int_val * item_b.int_val)};
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT64) {
        return push_l(*(long*)item_a.pointer * *(long*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_FLOAT) {
        printf("mul float: %g * %g\n", *(double*)item_a.pointer, *(double*)item_b.pointer);
        return push_d(*(double*)item_a.pointer * *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_FLOAT) {
        return push_d((double)item_a.int_val * *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_INT) {
        return push_d(*(double*)item_a.pointer * (double)item_b.int_val);
    }
    else if (item_a.type_id == LMD_TYPE_STRING && item_b.type_id == LMD_TYPE_INT) {
        String *str_a = (String*)item_a.pointer;
        String *result = str_repeat(str_a, item_b.int_val);
        return {.item = s2it(result)};
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_STRING) {
        String *str_b = (String*)item_b.pointer;
        String *result = str_repeat(str_b, item_a.int_val);
        return {.item = s2it(result)};
    }
    // Add libmpdec decimal support
    else if (item_a.type_id == LMD_TYPE_DECIMAL || item_b.type_id == LMD_TYPE_DECIMAL) {
        mpd_t* a_dec = convert_to_decimal(item_a, context->decimal_ctx);
        mpd_t* b_dec = convert_to_decimal(item_b, context->decimal_ctx);
        
        if (!a_dec || !b_dec) {
            if (a_dec) cleanup_temp_decimal(a_dec, item_a.type_id);
            if (b_dec) cleanup_temp_decimal(b_dec, item_b.type_id);
            printf("decimal conversion failed in fn_mul\n");
            return ItemError;
        }
        
        mpd_t* result = mpd_new(context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }
        
        mpd_mul(result, a_dec, b_dec, context->decimal_ctx);
        
        cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
        
        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            printf("decimal multiplication failed\n");
            return ItemError;
        }
        
        return push_decimal(result);
    }
    else if (item_a.type_id == LMD_TYPE_ARRAY_INT && item_b.type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr_a = item_a.array_int;
        ArrayInt* arr_b = item_b.array_int;
        if (arr_a->length != arr_b->length) {
            printf("Array length mismatch in multiplication\n");
            return ItemError;
        }
        ArrayInt* result = array_int_new(arr_a->length);
        for (long i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] * arr_b->items[i];
        }
        Item result_item;
        result_item.type_id = LMD_TYPE_ARRAY_INT;
        result_item.array_int = result;
        return result_item;
    }
    else if (item_a.type_id == LMD_TYPE_ARRAY_INT64 && item_b.type_id == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr_a = item_a.array_int64;
        ArrayInt64* arr_b = item_b.array_int64;
        if (arr_a->length != arr_b->length) {
            printf("Array length mismatch in multiplication\n");
            return ItemError;
        }
        ArrayInt64* result = array_int64_new(arr_a->length);
        for (long i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] * arr_b->items[i];
        }
        Item result_item;
        result_item.type_id = LMD_TYPE_ARRAY_INT64;
        result_item.array_int64 = result;
        return result_item;
    }
    else {
        printf("unknown mul type: %d, %d\n", item_a.type_id, item_b.type_id);
    }
    return ItemError;
}

Item fn_sub(Item item_a, Item item_b) {
    if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        return {.item = i2it(item_a.int_val - item_b.int_val)};
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT64) {
        return push_l(*(long*)item_a.pointer - *(long*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_FLOAT) {
        printf("sub float: %g - %g\n", *(double*)item_a.pointer, *(double*)item_b.pointer);
        return push_d(*(double*)item_a.pointer - *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_FLOAT) {
        return push_d((double)item_a.int_val - *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_INT) {
        return push_d(*(double*)item_a.pointer - (double)item_b.int_val);
    }
    // Add libmpdec decimal support
    else if (item_a.type_id == LMD_TYPE_DECIMAL || item_b.type_id == LMD_TYPE_DECIMAL) {
        mpd_t* a_dec = convert_to_decimal(item_a, context->decimal_ctx);
        mpd_t* b_dec = convert_to_decimal(item_b, context->decimal_ctx);
        
        if (!a_dec || !b_dec) {
            if (a_dec) cleanup_temp_decimal(a_dec, item_a.type_id);
            if (b_dec) cleanup_temp_decimal(b_dec, item_b.type_id);
            printf("decimal conversion failed in fn_sub\n");
            return ItemError;
        }
        
        mpd_t* result = mpd_new(context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }
        
        mpd_sub(result, a_dec, b_dec, context->decimal_ctx);
        
        cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
        
        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            printf("decimal subtraction failed\n");
            return ItemError;
        }
        
        return push_decimal(result);
    }
    else if (item_a.type_id == LMD_TYPE_ARRAY_INT && item_b.type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr_a = item_a.array_int;
        ArrayInt* arr_b = item_b.array_int;
        if (arr_a->length != arr_b->length) {
            printf("Array length mismatch in subtraction\n");
            return ItemError;
        }
        ArrayInt* result = array_int_new(arr_a->length);
        for (long i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] - arr_b->items[i];
        }
        Item result_item;
        result_item.type_id = LMD_TYPE_ARRAY_INT;
        result_item.array_int = result;
        return result_item;
    }
    else if (item_a.type_id == LMD_TYPE_ARRAY_INT64 && item_b.type_id == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr_a = item_a.array_int64;
        ArrayInt64* arr_b = item_b.array_int64;
        if (arr_a->length != arr_b->length) {
            printf("Array length mismatch in subtraction\n");
            return ItemError;
        }
        ArrayInt64* result = array_int64_new(arr_a->length);
        for (long i = 0; i < arr_a->length; i++) {
            result->items[i] = arr_a->items[i] - arr_b->items[i];
        }
        Item result_item;
        result_item.type_id = LMD_TYPE_ARRAY_INT64;
        result_item.array_int64 = result;
        return result_item;
    }
    else {
        printf("unknown sub type: %d, %d\n", item_a.type_id, item_b.type_id);
    }
    return ItemError;
}

Item fn_div(Item item_a, Item item_b) {
    if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        if (item_b.int_val == 0) {
            printf("integer division by zero error\n");
            return ItemError;
        }
        return push_d((double)item_a.int_val / (double)item_b.int_val);
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT64) {
        if (*(long*)item_b.pointer == 0) {
            printf("integer division by zero error\n");
            return ItemError;
        }
        return push_d((double)*(long*)item_a.pointer / (double)*(long*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_FLOAT) {
        if (*(double*)item_b.pointer == 0.0) {
            printf("float division by zero error\n");
            return ItemError;
        }
        printf("div float: %g / %g\n", *(double*)item_a.pointer, *(double*)item_b.pointer);
        return push_d(*(double*)item_a.pointer / *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_FLOAT) {
        if (*(double*)item_b.pointer == 0.0) {
            printf("float division by zero error\n");
            return ItemError;
        }
        return push_d((double)item_a.int_val / *(double*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT && item_b.type_id == LMD_TYPE_INT) {
        if (item_b.int_val == 0) {
            printf("integer division by zero error\n");
            return ItemError;
        }
        return push_d(*(double*)item_a.pointer / (double)item_b.int_val);
    }
    // Add libmpdec decimal support
    else if (item_a.type_id == LMD_TYPE_DECIMAL || item_b.type_id == LMD_TYPE_DECIMAL) {
        mpd_t* a_dec = convert_to_decimal(item_a, context->decimal_ctx);
        mpd_t* b_dec = convert_to_decimal(item_b, context->decimal_ctx);
        
        if (!a_dec || !b_dec) {
            if (a_dec) cleanup_temp_decimal(a_dec, item_a.type_id);
            if (b_dec) cleanup_temp_decimal(b_dec, item_b.type_id);
            printf("decimal conversion failed in fn_div\n");
            return ItemError;
        }
        
        // Check for division by zero
        printf("DEBUG: Checking division by zero, b_dec=%p\n", b_dec);
        if (b_dec) {
            char *b_str = mpd_to_sci(b_dec, 1);
            printf("DEBUG: b_dec value as string: '%s'\n", b_str ? b_str : "NULL");
            printf("DEBUG: mpd_iszero result: %d\n", mpd_iszero(b_dec));
            if (b_str) mpd_free(b_str);
        }
        if (b_dec && decimal_is_zero(b_dec)) {
            cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
            printf("decimal division by zero error\n");
            return ItemError;
        }
        printf("DEBUG: Division by zero check passed\n");
        
        // Debug the values and context before division
        if (a_dec && b_dec) {
            char *a_str = mpd_to_sci(a_dec, 1);
            char *b_str = mpd_to_sci(b_dec, 1);
            printf("DEBUG: About to divide '%s' / '%s'\n", a_str ? a_str : "NULL", b_str ? b_str : "NULL");
            printf("DEBUG: Context prec=%lld, emax=%lld, emin=%lld\n", (long long)context->decimal_ctx->prec, (long long)context->decimal_ctx->emax, (long long)context->decimal_ctx->emin);
            if (a_str) mpd_free(a_str);
            if (b_str) mpd_free(b_str);
        }
        
        mpd_t* result = mpd_new(context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }
        
        printf("DEBUG: Calling mpd_div...\n");
        mpd_div(result, a_dec, b_dec, context->decimal_ctx);
        printf("DEBUG: mpd_div completed\n");
        
        cleanup_temp_decimal(a_dec, item_a.type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(b_dec, item_b.type_id == LMD_TYPE_DECIMAL);
        
        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            printf("decimal division failed\n");
            return ItemError;
        }
        
        return push_decimal(result);
    }
    else {
        printf("unknown div type: %d, %d\n", item_a.type_id, item_b.type_id);
    }
    return ItemError;
}

Item fn_idiv(Item item_a, Item item_b) {
    // Check for division by zero
    bool is_zero = false;
    if (item_b.type_id == LMD_TYPE_INT) {
        is_zero = (item_b.int_val == 0);
    }
    else if (item_b.type_id == LMD_TYPE_INT64 && *(long*)item_b.pointer == 0) {
        is_zero = true;
    }
    
    if (is_zero) {
        printf("integer division by zero error\n");
        return ItemError;
    }

    if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        // Sign extend both values to proper signed longs
        long a_val = item_a.int_val, b_val = item_b.int_val;
        return (Item){.item = i2it(a_val / b_val)};
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT64) {
        return push_l(*(long*)item_a.pointer / *(long*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT64) {
        long a_val = item_a.int_val;
        return push_l(a_val / *(long*)item_b.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT) {
        long b_val = item_b.int_val;
        return push_l(*(long*)item_a.pointer / b_val);
    }
    else {
        printf("unknown idiv type: %d, %d\n", item_a.type_id, item_b.type_id);
    }
    return ItemError;
}

Item fn_pow(Item item_a, Item item_b) {
    // Handle decimal types first
    if (item_a.type_id == LMD_TYPE_DECIMAL || item_b.type_id == LMD_TYPE_DECIMAL) {
        // For now, convert decimals to double for power operations
        // This is a limitation of libmpdec - it doesn't have general power operations
        double base = 0.0, exponent = 0.0;
        
        if (item_a.type_id == LMD_TYPE_DECIMAL) {
            Decimal* dec_ptr = (Decimal*)item_a.pointer;
            // Convert decimal to string then to double (preserves precision better)
            char* str = mpd_to_sci(dec_ptr->dec_val, 1);
            base = strtod(str, NULL);
            free(str);
        } else {
            // Convert non-decimal to double
            if (item_a.type_id == LMD_TYPE_INT) {
                base = item_a.int_val;
            } else if (item_a.type_id == LMD_TYPE_INT64) {
                base = *(long*)item_a.pointer;
            } else if (item_a.type_id == LMD_TYPE_FLOAT) {
                base = *(double*)item_a.pointer;
            } else {
                printf("unsupported pow base type with decimal: %d\n", item_a.type_id);
                return ItemError;
            }
        }
        
        if (item_b.type_id == LMD_TYPE_DECIMAL) {
            Decimal* dec_ptr = (Decimal*)item_b.pointer;
            char* str = mpd_to_sci(dec_ptr->dec_val, 1);
            exponent = strtod(str, NULL);
            free(str);
        } else {
            // Convert non-decimal to double
            if (item_b.type_id == LMD_TYPE_INT) {
                exponent = item_b.int_val;
            } else if (item_b.type_id == LMD_TYPE_INT64) {
                exponent = *(long*)item_b.pointer;
            } else if (item_b.type_id == LMD_TYPE_FLOAT) {
                exponent = *(double*)item_b.pointer;
            } else {
                printf("unsupported pow exponent type with decimal: %d\n", item_b.type_id);
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
            printf("decimal power operation failed\n");
            return ItemError;
        }
        
        return push_decimal(result);
    }
    
    // Original non-decimal logic
    double base = 0.0, exponent = 0.0;
    
    // Convert first argument to double
    if (item_a.type_id == LMD_TYPE_INT) {
        base = item_a.int_val;
    }
    else if (item_a.type_id == LMD_TYPE_INT64) {
        base = *(long*)item_a.pointer;
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT) {
        base = *(double*)item_a.pointer;
    }
    else {
        printf("unknown pow base type: %d\n", item_a.type_id);
        return ItemError;
    }
    
    // Convert second argument to double
    if (item_b.type_id == LMD_TYPE_INT) {
        exponent = item_b.int_val;
    }
    else if (item_b.type_id == LMD_TYPE_INT64) {
        exponent = *(long*)item_b.pointer;
    }
    else if (item_b.type_id == LMD_TYPE_FLOAT) {
        exponent = *(double*)item_b.pointer;
    }
    else {
        printf("unknown pow exponent type: %d\n", item_b.type_id);
        return ItemError;
    }
    return push_d(pow(base, exponent));
}

Item fn_mod(Item item_a, Item item_b) {
    // Handle decimal types first
    if (item_a.type_id == LMD_TYPE_DECIMAL || item_b.type_id == LMD_TYPE_DECIMAL) {
        mpd_t* val_a = convert_to_decimal(item_a, context->decimal_ctx);
        if (!val_a) return ItemError;
        
        mpd_t* val_b = convert_to_decimal(item_b, context->decimal_ctx);
        if (!val_b) {
            cleanup_temp_decimal(val_a, item_a.type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }
        
        // Check for division by zero
        if (decimal_is_zero(val_b)) {
            printf("modulo by zero error\n");
            cleanup_temp_decimal(val_a, item_a.type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(val_b, item_b.type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }
        
        mpd_t* result = mpd_new(context->decimal_ctx);
        if (!result) {
            cleanup_temp_decimal(val_a, item_a.type_id == LMD_TYPE_DECIMAL);
            cleanup_temp_decimal(val_b, item_b.type_id == LMD_TYPE_DECIMAL);
            return ItemError;
        }
        
        mpd_rem(result, val_a, val_b, context->decimal_ctx);
        
        // Clean up temporary decimals
        cleanup_temp_decimal(val_a, item_a.type_id == LMD_TYPE_DECIMAL);
        cleanup_temp_decimal(val_b, item_b.type_id == LMD_TYPE_DECIMAL);
        
        if (mpd_isnan(result) || mpd_isinfinite(result)) {
            mpd_del(result);
            printf("decimal modulo operation failed\n");
            return ItemError;
        }
        
        return push_decimal(result);
    }
    
    // Original non-decimal logic for integer mod
    if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT) {
        // Sign extend both values to proper signed longs
        long a_val = item_a.int_val, b_val = item_b.int_val;
        if (b_val == 0) {
            printf("modulo by zero error\n");
            return ItemError;
        }
        return (Item){.item = i2it(a_val % b_val)};
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT64) {
        long a_val = *(long*)item_a.pointer, b_val = *(long*)item_b.pointer;
        if (b_val == 0) {
            printf("modulo by zero error\n");
            return ItemError;
        }
        return push_l(a_val % b_val);
    }
    else if (item_a.type_id == LMD_TYPE_INT && item_b.type_id == LMD_TYPE_INT64) {
        long a_val = item_a.int_val, b_val = *(long*)item_b.pointer;
        if (b_val == 0) {
            printf("modulo by zero error\n");
            return ItemError;
        }
        return push_l(a_val % b_val);
    }
    else if (item_a.type_id == LMD_TYPE_INT64 && item_b.type_id == LMD_TYPE_INT) {
        long a_val = *(long*)item_a.pointer, b_val = item_b.int_val;
        if (b_val == 0) {
            printf("modulo by zero error\n");
            return ItemError;
        }
        return push_l(a_val % b_val);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT || item_b.type_id == LMD_TYPE_FLOAT) {
        printf("modulo not supported for float types\n");
        return ItemError;
    }
    else {
        printf("unknown mod type: %d, %d\n", item_a.type_id, item_b.type_id);
        return ItemError;
    }
}

// Numeric system functions implementation

Item fn_abs(Item item) {
    // abs() - absolute value of a number
    if (item.type_id == LMD_TYPE_INT) {
        long val = item.int_val;
        return (Item){.item = i2it(val < 0 ? -val : val)};
    }
    else if (item.type_id == LMD_TYPE_INT64) {
        long val = *(long*)item.pointer;
        return push_l(val < 0 ? -val : val);
    }
    else if (item.type_id == LMD_TYPE_FLOAT) {
        double val = *(double*)item.pointer;
        return push_d(fabs(val));
    }
    else {
        printf("abs not supported for type: %d\n", item.type_id);
        return ItemError;
    }
}

Item fn_round(Item item) {
    // round() - round to nearest integer
    if (item.type_id == LMD_TYPE_INT || item.type_id == LMD_TYPE_INT64) {
        // Already an integer, return as-is
        return item;
    }
    else if (item.type_id == LMD_TYPE_FLOAT) {
        double val = *(double*)item.pointer;
        return push_d(round(val));
    }
    else {
        printf("round not supported for type: %d\n", item.type_id);
        return ItemError;
    }
}

Item fn_floor(Item item) {
    // floor() - round down to nearest integer
    if (item.type_id == LMD_TYPE_INT || item.type_id == LMD_TYPE_INT64) {
        return item;  // return as-is
    }
    else if (item.type_id == LMD_TYPE_FLOAT) {
        double val = *(double*)item.pointer;
        return push_d(floor(val));
    }
    else {
        printf("floor not supported for type: %d\n", item.type_id);
        return ItemError;
    }
}

Item fn_ceil(Item item) {
    // ceil() - round up to nearest integer
    if (item.type_id == LMD_TYPE_INT || item.type_id == LMD_TYPE_INT64) {
        return item;  // return as-is
    }
    else if (item.type_id == LMD_TYPE_FLOAT) {
        double val = *(double*)item.pointer;
        return push_d(ceil(val));
    }
    else {
        printf("ceil not supported for type: %d\n", item.type_id);
        return ItemError;
    }
}

Item fn_min(Item item_a, Item item_b) {
    // Check if we're being called with array min (single array argument)
    // This happens when the second argument is ITEM_NULL or has type LMD_TYPE_NULL
    if (item_b.type_id == LMD_TYPE_NULL || item_b.raw_pointer == NULL) {
        // Single argument array min case
        TypeId type_id = get_type_id(item_a);
        if (type_id == LMD_TYPE_ARRAY_FLOAT) {
            ArrayFloat* arr = item_a.array_float;
            if (!arr || arr->length == 0) {
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
        else if (type_id == LMD_TYPE_ARRAY_INT) {
            ArrayInt* arr = item_a.array_int;
            if (!arr || arr->length == 0) {
                return ItemError; // Empty array has no minimum
            }
            int32_t min_val = arr->items[0];
            for (size_t i = 1; i < arr->length; i++) {
                if (arr->items[i] < min_val) {
                    min_val = arr->items[i];
                }
            }
            return {.item = i2it(min_val)};
        }
        else if (type_id == LMD_TYPE_ARRAY_INT64) {
            ArrayInt64* arr = item_a.array_int64;
            if (!arr || arr->length == 0) {
                return ItemError; // Empty array has no minimum
            }
            int64_t min_val = arr->items[0];
            for (size_t i = 1; i < arr->length; i++) {
                if (arr->items[i] < min_val) {
                    min_val = arr->items[i];
                }
            }
            return push_l(min_val);
        }
        else if (type_id == LMD_TYPE_ARRAY) {
            Array* arr = item_a.array;
            if (!arr || arr->length == 0) {
                return ItemError; // Empty array has no minimum
            }
            Item min_item = array_get(arr, 0);
            double min_val = 0.0;
            bool is_float = false;
            
            // Convert first element
            if (min_item.type_id == LMD_TYPE_INT) {
                min_val = (double)min_item.int_val;
            }
            else if (min_item.type_id == LMD_TYPE_INT64) {
                min_val = (double)(*(long*)min_item.pointer);
            }
            else if (min_item.type_id == LMD_TYPE_FLOAT) {
                min_val = *(double*)min_item.pointer;
                is_float = true;
            }
            else {
                return ItemError;
            }
            
            // Find minimum
            for (size_t i = 1; i < arr->length; i++) {
                Item elem_item = array_get(arr, i);
                double elem_val = 0.0;
                
                if (elem_item.type_id == LMD_TYPE_INT) {
                    elem_val = (double)elem_item.int_val;
                }
                else if (elem_item.type_id == LMD_TYPE_INT64) {
                    elem_val = (double)(*(long*)elem_item.pointer);
                }
                else if (elem_item.type_id == LMD_TYPE_FLOAT) {
                    elem_val = *(double*)elem_item.pointer;
                    is_float = true;
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
            } else {
                return (Item){.item = i2it((long)min_val)};
            }
        }
        else {
            printf("min not supported for single argument type: %d\n", type_id);
            return ItemError;
        }
    }
    
    // Two argument scalar min case (original behavior)
    double a_val = 0.0, b_val = 0.0;
    bool is_float = false;
    
    // Convert first argument
    if (item_a.type_id == LMD_TYPE_INT) {
        a_val = item_a.int_val;
    }
    else if (item_a.type_id == LMD_TYPE_INT64) {
        a_val = (double)(*(long*)item_a.pointer);
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT) {
        a_val = *(double*)item_a.pointer;
        is_float = true;
    }
    else {
        printf("min not supported for type: %d\n", item_a.type_id);
        return ItemError;
    }
    
    // Convert second argument
    if (item_b.type_id == LMD_TYPE_INT) {
        b_val = item_b.int_val;
    }
    else if (item_b.type_id == LMD_TYPE_INT64) {
        b_val = *(long*)item_b.pointer;
    }
    else if (item_b.type_id == LMD_TYPE_FLOAT) {
        b_val = *(double*)item_b.pointer;
        is_float = true;
    }
    else {
        printf("min not supported for type: %d\n", item_b.type_id);
        return ItemError;
    }
    
    double result = a_val < b_val ? a_val : b_val;
    
    // Return as integer if both inputs were integers
    if (!is_float) {
        // Convert back to Item int directly to match input type
        return {.item = i2it((long)result)};
    } else {
        return push_d(result);
    }
}

Item fn_max(Item item_a, Item item_b) {
    // Check if we're being called with array max (single array argument)
    // This happens when the second argument is ITEM_NULL or has type LMD_TYPE_NULL
    if (item_b.type_id == LMD_TYPE_NULL || item_b.raw_pointer == NULL) {
        // Single argument array max case
        TypeId type_id = get_type_id(item_a);
        if (type_id == LMD_TYPE_ARRAY_FLOAT) {
            ArrayFloat* arr = item_a.array_float;
            if (!arr || arr->length == 0) {
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
            if (!arr || arr->length == 0) {
                return ItemError; // Empty array has no maximum
            }
            int32_t max_val = arr->items[0];
            for (size_t i = 1; i < arr->length; i++) {
                if (arr->items[i] > max_val) {
                    max_val = arr->items[i];
                }
            }
            return {.item = i2it(max_val)};
        }
        else if (type_id == LMD_TYPE_ARRAY_INT64) {
            ArrayInt64* arr = item_a.array_int64;
            if (!arr || arr->length == 0) {
                return ItemError; // Empty array has no maximum
            }
            int64_t max_val = arr->items[0];
            for (size_t i = 1; i < arr->length; i++) {
                if (arr->items[i] > max_val) {
                    max_val = arr->items[i];
                }
            }
            return push_l(max_val);
        }
        else if (type_id == LMD_TYPE_ARRAY) {
            Array* arr = item_a.array;
            if (!arr || arr->length == 0) {
                return ItemError; // Empty array has no maximum
            }
            Item max_item = array_get(arr, 0);
            double max_val = 0.0;
            bool is_float = false;
            
            // Convert first element
            if (max_item.type_id == LMD_TYPE_INT) {
                max_val = (double)max_item.int_val;
            }
            else if (max_item.type_id == LMD_TYPE_INT64) {
                max_val = (double)(*(long*)max_item.pointer);
            }
            else if (max_item.type_id == LMD_TYPE_FLOAT) {
                max_val = *(double*)max_item.pointer;
                is_float = true;
            }
            else {
                return ItemError;
            }
            
            // Find maximum
            for (size_t i = 1; i < arr->length; i++) {
                Item elem_item = array_get(arr, i);
                double elem_val = 0.0;
                
                if (elem_item.type_id == LMD_TYPE_INT) {
                    elem_val = (double)elem_item.int_val;
                }
                else if (elem_item.type_id == LMD_TYPE_INT64) {
                    elem_val = (double)(*(long*)elem_item.pointer);
                }
                else if (elem_item.type_id == LMD_TYPE_FLOAT) {
                    elem_val = *(double*)elem_item.pointer;
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
            } else {
                return (Item){.item = i2it((long)max_val)};
            }
        }
        else {
            printf("max not supported for single argument type: %d\n", type_id);
            return ItemError;
        }
    }
    
    // Two argument scalar max case (original behavior)
    double a_val = 0.0, b_val = 0.0;
    bool is_float = false;
    
    // Convert first argument
    if (item_a.type_id == LMD_TYPE_INT) {
        a_val = item_a.int_val;
    }
    else if (item_a.type_id == LMD_TYPE_INT64) {
        a_val = *(long*)item_a.pointer;
    }
    else if (item_a.type_id == LMD_TYPE_FLOAT) {
        a_val = *(double*)item_a.pointer;
        is_float = true;
    }
    else {
        printf("max not supported for type: %d\n", item_a.type_id);
        return ItemError;
    }
    
    // Convert second argument
    if (item_b.type_id == LMD_TYPE_INT) {
        b_val = item_b.int_val;
    }
    else if (item_b.type_id == LMD_TYPE_INT64) {
        b_val = *(long*)item_b.pointer;
    }
    else if (item_b.type_id == LMD_TYPE_FLOAT) {
        b_val = *(double*)item_b.pointer;
        is_float = true;
    }
    else {
        printf("max not supported for type: %d\n", item_b.type_id);
        return ItemError;
    }
    
    double result = a_val > b_val ? a_val : b_val;
    
    // Return as integer if both inputs were integers
    if (!is_float) {
        // Convert back to Item int directly to match input type
        return {.item = i2it((long)result)};
    } else {
        return push_d(result);
    }
}

Item fn_sum(Item item) {
    // sum() - sum of all elements in an array or list
    TypeId type_id = get_type_id(item);
    printf("DEBUG fn_sum: called with type_id: %d, pointer: %p\n", type_id, item.raw_pointer);
    if (type_id == LMD_TYPE_ARRAY) {
        printf("DEBUG fn_sum: Processing LMD_TYPE_ARRAY\n");
        Array* arr = item.array;  // Use item.array, not item.pointer
        printf("DEBUG fn_sum: Array pointer: %p, length: %ld\n", arr, arr ? arr->length : -1);
        if (!arr || arr->length == 0) {
            printf("DEBUG fn_sum: Empty array, returning 0\n");
            return (Item){.item = i2it(0)};  // Empty array sums to 0
        }
        double sum = 0.0;
        bool has_float = false;
        for (size_t i = 0; i < arr->length; i++) {
            Item elem_item = array_get(arr, i);
            if (elem_item.type_id == LMD_TYPE_INT) {
                long val = elem_item.int_val;
                printf("DEBUG fn_sum: Adding int value: %ld\n", val);
                sum += (double)val;
            }
            else if (elem_item.type_id == LMD_TYPE_INT64) {
                long val = *(long*)elem_item.pointer;
                printf("DEBUG fn_sum: Adding int64 value: %ld\n", val);
                sum += (double)val;
            }
            else if (elem_item.type_id == LMD_TYPE_FLOAT) {
                double val = *(double*)elem_item.pointer;
                printf("DEBUG fn_sum: Adding float value: %f\n", val);
                sum += val;
                has_float = true;
            }
            else {
                printf("DEBUG fn_sum: sum: non-numeric element at index %zu, type: %d\n", i, elem_item.type_id);
                return ItemError;
            }
        }
        if (has_float) {
            return push_d(sum);
        } else {
            return push_l((long)sum);
        }
    }
    else if (type_id == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr = item.array_int;  // Use the correct field
        if (!arr || arr->length == 0) {
            return (Item){.item = i2it(0)};  // Empty array sums to 0
        }
        int64_t sum = 0;
        for (size_t i = 0; i < arr->length; i++) {
            sum += arr->items[i];
        }
        return push_l(sum);
    }
    else if (type_id == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr = item.array_int64;
        if (!arr || arr->length == 0) {
            return (Item){.item = i2it(0)};  // Empty array sums to 0
        }
        int64_t sum = 0;
        for (size_t i = 0; i < arr->length; i++) {
            sum += arr->items[i];
        }
        return push_l(sum);
    }
    else if (type_id == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr = item.array_float;  // Use the correct field
        if (!arr || arr->length == 0) {
            return push_d(0.0);  // Empty array sums to 0.0
        }
        double sum = 0.0;
        for (size_t i = 0; i < arr->length; i++) {
            sum += arr->items[i];
        }
        return push_d(sum);
    }
    else if (type_id == LMD_TYPE_LIST) {
        printf("DEBUG fn_sum: Processing LMD_TYPE_LIST\n");
        List* list = item.list;
        printf("DEBUG fn_sum: List pointer: %p, length: %ld\n", list, list ? list->length : -1);
        if (!list || list->length == 0) {
            printf("DEBUG fn_sum: Empty list, returning 0\n");
            return (Item){.item = i2it(0)};  // Empty list sums to 0
        }
        double sum = 0.0;
        bool has_float = false;
        for (size_t i = 0; i < list->length; i++) {
            Item elem_item = list_get(list, i);
            if (elem_item.type_id == LMD_TYPE_INT) {
                long val = elem_item.int_val;
                printf("DEBUG fn_sum: Adding int value: %ld\n", val);
                sum += (double)val;
            }
            else if (elem_item.type_id == LMD_TYPE_INT64) {
                long val = *(long*)elem_item.pointer;
                printf("DEBUG fn_sum: Adding int64 value: %ld\n", val);
                sum += (double)val;
            }
            else if (elem_item.type_id == LMD_TYPE_FLOAT) {
                double val = *(double*)elem_item.pointer;
                printf("DEBUG fn_sum: Adding float value: %f\n", val);
                sum += val;
                has_float = true;
            }
            else {
                printf("DEBUG fn_sum: sum: non-numeric element at index %zu, type: %d\n", i, elem_item.type_id);
                return ItemError;
            }
        }
        if (has_float) {
            printf("DEBUG fn_sum: Returning sum as double: %f\n", sum);
            return push_d(sum);
        } else {
            printf("DEBUG fn_sum: Returning sum as long: %ld\n", (long)sum);
            return push_l((long)sum);
        }
    }
    else {
        printf("DEBUG fn_sum: sum not supported for type: %d\n", type_id);
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
            if (elem_item.type_id == LMD_TYPE_INT) {
                long val = elem_item.int_val;
                sum += (double)val;
            }
            else if (elem_item.type_id == LMD_TYPE_INT64) {
                sum += (double)(*(long*)elem_item.pointer);
            }
            else if (elem_item.type_id == LMD_TYPE_FLOAT) {
                sum += *(double*)elem_item.pointer;
            }
            else {
                printf("avg: non-numeric element at index %zu, type: %d\n", i, elem_item.type_id);
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
            if (elem_item.type_id == LMD_TYPE_INT) {
                long val = (long)elem_item.int_val;
                sum += (double)val;
            }
            else if (elem_item.type_id == LMD_TYPE_INT64) {
                sum += (double)(*(long*)elem_item.pointer);
            }
            else if (elem_item.type_id == LMD_TYPE_FLOAT) {
                sum += *(double*)elem_item.pointer;
            }
            else {
                printf("avg: non-numeric element at index %zu, type: %d\n", i, elem_item.type_id);
                return ItemError;
            }
        }
        return push_d(sum / (double)list->length);
    }
    else {
        printf("avg not supported for type: %d\n", type_id);
        return ItemError;
    }
}

Item fn_pos(Item item) {
    // Unary + operator - return the item as-is for numeric types, or cast strings/symbols to numbers
    if (item.type_id == LMD_TYPE_INT) {
        return item;  // Already in correct format
    }
    else if (item.type_id == LMD_TYPE_INT64) {
        return item;  // Already in correct format
    }
    else if (item.type_id == LMD_TYPE_FLOAT) {
        return item;  // Already in correct format
    }
    else if (item.type_id == LMD_TYPE_DECIMAL) {
        return item;  // For decimal, unary + returns the same value
    }
    else if (item.type_id == LMD_TYPE_STRING || item.type_id == LMD_TYPE_SYMBOL) {
        // Cast string/symbol to number
        String* str = (String*)item.pointer;
        if (!str || str->len == 0) {
            printf("unary + error: empty string/symbol\n");
            return ItemError;
        }
        
        // Try to parse as integer first
        char* endptr;
        long long_val = strtol(str->chars, &endptr, 10);
        
        // If entire string was consumed and no overflow, it's an integer
        if (*endptr == '\0' && endptr == str->chars + str->len) {
            return (Item){.item = i2it(long_val)};
        }
        
        // Try to parse as float
        double double_val = strtod(str->chars, &endptr);
        if (*endptr == '\0' && endptr == str->chars + str->len) {
            return push_d(double_val);
        }
        
        // Not a valid number
        printf("unary + error: cannot convert '%.*s' to number\n", (int)str->len, str->chars);
        return ItemError;
    }
    else {
        // For other types (bool, datetime, etc.), unary + is an error
        printf("unary + not supported for type: %d\n", item.type_id);
        return ItemError;
    }
}

Item fn_neg(Item item) {
    // Unary - operator - negate numeric values or cast and negate strings/symbols
    if (item.type_id == LMD_TYPE_INT) {
        // Sign extend the 56-bit long_val to a proper signed long, then negate
        int val = item.int_val;
        return (Item){.item = i2it(-val)};
    }
    else if (item.type_id == LMD_TYPE_INT64) {
        long val = *(long*)item.pointer;
        return push_l(-val);
    }
    else if (item.type_id == LMD_TYPE_FLOAT) {
        double val = *(double*)item.pointer;
        return push_d(-val);
    }
    else if (item.type_id == LMD_TYPE_DECIMAL) {
        // For decimal types, we'd need to negate the libmpdec value
        // This would require more complex decimal arithmetic with libmpdec
        printf("unary - for decimal type not yet implemented\n");
        return ItemError;
    }
    else if (item.type_id == LMD_TYPE_STRING || item.type_id == LMD_TYPE_SYMBOL) {
        // Cast string/symbol to number, then negate
        String* str = (String*)item.pointer;
        if (!str || str->len == 0) {
            printf("unary - error: empty string/symbol\n");
            return ItemError;
        }
        
        // Try to parse as integer first
        char* endptr;
        long long_val = strtol(str->chars, &endptr, 10);
        
        // If entire string was consumed and no overflow, it's an integer
        if (*endptr == '\0' && endptr == str->chars + str->len) {
            return (Item){.item = i2it(-long_val)};
        }
        
        // Try to parse as float
        double double_val = strtod(str->chars, &endptr);
        if (*endptr == '\0' && endptr == str->chars + str->len) {
            return push_d(-double_val);
        }
        
        // Not a valid number
        printf("unary - error: cannot convert '%.*s' to number\n", (int)str->len, str->chars);
        return ItemError;
    }
    else {
        // For other types (bool, datetime, etc.), unary - is an error
        printf("unary - not supported for type: %d\n", item.type_id);
        return ItemError;
    }
}

Item fn_int(Item item) {
    double dval;
    if (item.type_id == LMD_TYPE_INT) {
        return item;
    }
    else if (item.type_id == LMD_TYPE_INT64) {
        dval = *(int64_t*)item.pointer;
        goto CHECK_DVAL;
    }
    else if (item.type_id == LMD_TYPE_FLOAT) {
        // cast down to int
        dval = *(double*)item.pointer;
        CHECK_DVAL:
        if (dval > INT32_MAX || dval < INT32_MIN) {
            // Promote to decimal if out of int32 range
            printf("promote float to decimal: %g\n", dval);
            mpd_t* dec_val = mpd_new(context->decimal_ctx);
            if (!dec_val) {
                printf("Failed to allocate decimal for float conversion\n");
                return ItemError;
            }
            char str_buf[64];
            snprintf(str_buf, sizeof(str_buf), "%.17g", dval);
            mpd_set_string(dec_val, str_buf, context->decimal_ctx);
            return push_decimal(dec_val);
        }
        return {._type= LMD_TYPE_INT, .int_val = (int32_t)dval};
    }
    else if (item.type_id == LMD_TYPE_DECIMAL) {
        return item;  // keep it as it is
    }
    else if (item.type_id == LMD_TYPE_STRING || item.type_id == LMD_TYPE_SYMBOL) {
        String* str = (String*)item.pointer;
        if (!str || str->len == 0) {
            return ItemError;
        }
        char* endptr;
        // try to parse as int32 first
        errno = 0;  // clear errno before calling strtoll
        int32_t val = strtol(str->chars, &endptr, 10);
        if (endptr == str->chars) {
            printf("Cannot convert string '%s' to int\n", str->chars);
            return ItemError;
        }
        // check for overflow - if errno is set or we couldn't parse the full string
        if (errno == ERANGE || (*endptr != '\0')) {
            // try to parse as decimal
            mpd_t* dec_val = mpd_new(context->decimal_ctx);
            if (!dec_val) {
                printf("Failed to allocate decimal for string conversion\n");
                return ItemError;
            }
            mpd_set_string(dec_val, str->chars, context->decimal_ctx);
            if (mpd_isnan(dec_val) || mpd_isinfinite(dec_val)) {
                printf("Cannot convert string '%s' to decimal\n", str->chars);
                mpd_del(dec_val);
                return ItemError;
            }
            printf("promote string to decimal: %s\n", str->chars);
            return push_decimal(dec_val);
        }
        return {._type= LMD_TYPE_INT, .int_val = val};
    }
    else {
        printf("Cannot convert type %d to int\n", item.type_id);
        return ItemError;
    }
}

int64_t fn_int64(Item item) {
    // Convert item to int64
    int64_t val;
    if (item.type_id == LMD_TYPE_INT) {
        printf("convert int to int64: %d\n", item.int_val);
        return item.int_val;
    }
    else if (item.type_id == LMD_TYPE_INT64) {
        return *(int64_t*)item.pointer;
    }
    else if (item.type_id == LMD_TYPE_FLOAT) {
        double dval = *(double*)item.pointer;
        if (dval > LAMBDA_INT64_MAX || dval < INT64_MIN) {
            printf("float value %g out of int64 range\n", dval);
            return INT_ERROR;
        }
        return (int64_t)dval;
    }
    else if (item.type_id == LMD_TYPE_DECIMAL) {
        // Convert decimal to int64
        Decimal* dec_ptr = (Decimal*)item.pointer;
        mpd_t* dec = dec_ptr->dec_val;
        if (!dec) {
            printf("decimal pointer is NULL\n");
            return INT_ERROR;
        }
        char* endptr;
        char* dec_str = mpd_to_sci(dec, 1);
        if (!dec_str) {
            printf("mpd_to_sci failed\n");
            return INT_ERROR;
        }
        printf("convert decimal to int64: %s\n", dec_str);
        val = strtoll(dec_str, &endptr, 10);
        mpd_free(dec_str);
        if (endptr == dec_str) {
            printf("Cannot convert decimal to int64\n");
            return INT_ERROR;
        }
        return val;
    }
    else if (item.type_id == LMD_TYPE_STRING || item.type_id == LMD_TYPE_SYMBOL) {
        String* str = (String*)item.pointer;
        if (!str || str->len == 0) {
            return 0;
        }
        char* endptr;
        printf("convert string/symbol to int64: %s\n", str->chars);
        errno = 0;  // clear errno before calling strtoll
        int64_t val = strtoll(str->chars, &endptr, 10);
        if (endptr == str->chars) {
            printf("Cannot convert string '%s' to int64\n", str->chars);
            return INT_ERROR;
        }
        if (errno == ERANGE) {
            printf("String value '%s' out of int64 range\n", str->chars);
            return INT_ERROR;
        }
        printf("converted string to int64: %" PRId64 "\n", val);
        return val;
    }
    printf("Cannot convert type %d to int64\n", item.type_id);
    return INT_ERROR;
}

// Unicode string normalization function
Item fn_normalize(Item str_item, Item type_item) {
    // normalize(string, 'nfc'|'nfd'|'nfkc'|'nfkd') - Unicode normalization
    if (str_item.type_id != LMD_TYPE_STRING) {
        printf("normalize: first argument must be a string, got type: %d\n", str_item.type_id);
        return ItemError;
    }
    
    String* str = (String*)str_item.pointer;
    if (!str || str->len == 0) {
        return str_item;  // Return empty string as-is
    }
    
    // Default to NFC if no type specified or invalid type
    int options = UTF8PROC_STABLE | UTF8PROC_COMPOSE;
    
    if (type_item.type_id == LMD_TYPE_STRING) {
        String* type_str = (String*)type_item.pointer;
        if (type_str && type_str->len > 0) {
            if (strncmp(type_str->chars, "nfd", 3) == 0) {
                options = UTF8PROC_STABLE | UTF8PROC_DECOMPOSE;
            } else if (strncmp(type_str->chars, "nfkc", 4) == 0) {
                options = UTF8PROC_STABLE | UTF8PROC_COMPOSE | UTF8PROC_COMPAT;
            } else if (strncmp(type_str->chars, "nfkd", 4) == 0) {
                options = UTF8PROC_STABLE | UTF8PROC_DECOMPOSE | UTF8PROC_COMPAT;
            }
            // Default case (nfc) already set above
        }
    }
    
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_UTF8PROC
    // Use utf8proc for Unicode normalization
    utf8proc_uint8_t* normalized = NULL;
    utf8proc_ssize_t normalized_len = utf8proc_map(
        (const utf8proc_uint8_t*)str->chars, str->len, &normalized, (utf8proc_option_t)options);
    
    if (normalized_len < 0) {
        printf("normalize: utf8proc_map failed with error: %zd\n", normalized_len);
        return ItemError;
    }
    
    // Create new string with normalized content
    String* result = (String*)heap_alloc(sizeof(String) + normalized_len + 1, LMD_TYPE_STRING);
    result->ref_cnt = 0;
    result->len = normalized_len;
    memcpy(result->chars, normalized, normalized_len);
    result->chars[normalized_len] = '\0';
    
    // Free the utf8proc allocated buffer
    free(normalized);
    
    return (Item){.item = s2it(result)};
    
#elif LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    // Use ICU for normalization (fallback for compatibility)
    printf("normalize: ICU normalization not implemented yet\n");
    return ItemError;
    
#else
    // ASCII-only mode: return original string (no normalization)
    printf("normalize: Unicode normalization disabled (ASCII-only mode)\n");
    return str_item;  // Return original string unchanged
#endif
}

Range* fn_to(Item item_a, Item item_b) {
    // todo: join binary, list, array, map
    if ((item_a.type_id == LMD_TYPE_INT || item_a.type_id == LMD_TYPE_INT64) && 
        (item_b.type_id == LMD_TYPE_INT || item_b.type_id == LMD_TYPE_INT64)) {
        long start = item_a.type_id == LMD_TYPE_INT ? item_a.int_val : *(long*)item_a.pointer;
        long end = item_b.type_id == LMD_TYPE_INT ? item_b.int_val : *(long*)item_b.pointer;
        if (start > end) {
            // todo: should raise error
            printf("Error: start of range is greater than end: %ld > %ld\n", start, end);
            return NULL;
        }
        Range *range = (Range *)heap_alloc(sizeof(Range), LMD_TYPE_RANGE);
        range->type_id = LMD_TYPE_RANGE;
        range->start = start;  range->end = end;
        range->length = end - start + 1;
        printf("create range: %ld to %ld, length: %ld\n", range->start, range->end, range->length);
        return range;
    }
    else {
        printf("unknown range type: %d, %d\n", item_a.type_id, item_b.type_id);
        return NULL;
    }
}

int64_t it2l(Item itm) {
    if (itm.type_id == LMD_TYPE_INT) {
        return itm.int_val;
    }
    else if (itm.type_id == LMD_TYPE_INT64) {
        return *(int64_t*)itm.pointer;
    }
    else if (itm.type_id == LMD_TYPE_FLOAT) {
        return (int64_t)*(double*)itm.pointer;
    }
    printf("invalid type %d\n", itm.type_id);
    // todo: push error
    return 0;
}

double it2d(Item itm) {
    if (itm.type_id == LMD_TYPE_INT) {
        return itm.int_val;
    }
    else if (itm.type_id == LMD_TYPE_INT64) {
        return *(long*)itm.pointer;
    }
    else if (itm.type_id == LMD_TYPE_FLOAT) {
        return *(double*)itm.pointer;
    }
    printf("invalid type %d\n", itm.type_id);
    // todo: push error
    return 0;
}

Function* to_fn(fn_ptr ptr) {
    printf("create fn %p\n", ptr);
    Function *fn = (Function*)calloc(1, sizeof(Function));
    fn->type_id = LMD_TYPE_FUNC;
    fn->ptr = ptr;
    return fn;
}

bool fn_is(Item a, Item b) {
    printf("is expr\n");
    TypeId b_type_id = get_type_id(b);
    if (b_type_id != LMD_TYPE_TYPE) {
        return false;
    }
    TypeType *type_b = (TypeType *)b.type;
    TypeId a_type_id = get_type_id(a);
    printf("is type %d, %d\n", a_type_id, type_b->type->type_id);
    switch (type_b->type->type_id) {
    case LMD_TYPE_ANY:
        return a_type_id != LMD_TYPE_ERROR;
    case LMD_TYPE_INT:  case LMD_TYPE_INT64:  case LMD_TYPE_FLOAT:  case LMD_TYPE_NUMBER:
        return LMD_TYPE_INT <= a_type_id && a_type_id <= type_b->type->type_id;
    case LMD_TYPE_RANGE:  case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_INT:  case LMD_TYPE_ARRAY_FLOAT:
        printf("is array type: %d, %d\n", a_type_id, type_b->type->type_id);
        return a_type_id == LMD_TYPE_RANGE || a_type_id == LMD_TYPE_ARRAY || a_type_id == LMD_TYPE_ARRAY_INT || a_type_id == LMD_TYPE_ARRAY_FLOAT;
    default:
        return a_type_id == type_b->type->type_id;
    }
}

bool equal(Item a_item, Item b_item) {
    CompResult result = equal_comp(a_item, b_item);
    return result == COMP_TRUE;
}

CompResult equal_comp(Item a_item, Item b_item) {
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_UTF8PROC
    // Use utf8proc Unicode-enhanced comparison
    return equal_comp_utf8proc(a_item, b_item);
#elif LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    // Use ICU Unicode-enhanced comparison (deprecated)
    return equal_comp_unicode(a_item, b_item);
#else
    // Original ASCII-only implementation
    printf("equal_comp expr\n");
    if (a_item.type_id != b_item.type_id) {
        // number promotion - only for int/float types
        if (LMD_TYPE_INT <= a_item.type_id && a_item.type_id <= LMD_TYPE_NUMBER && 
            LMD_TYPE_INT <= b_item.type_id && b_item.type_id <= LMD_TYPE_NUMBER) {
            double a_val = it2d(a_item), b_val = it2d(b_item);
            return (a_val == b_val) ? COMP_TRUE : COMP_FALSE;
        }
        // Type mismatch error for equality comparisons (e.g., true == 1, "test" != null)
        // Note: null can only be compared to null, any other comparison is a type error
        return COMP_ERROR;
    }
    
    if (a_item.type_id == LMD_TYPE_NULL) {
        return COMP_TRUE; // null == null
    }    
    else if (a_item.type_id == LMD_TYPE_BOOL) {
        return (a_item.bool_val == b_item.bool_val) ? COMP_TRUE : COMP_FALSE;
    }
    else if (a_item.type_id == LMD_TYPE_INT) {
        return (a_item.int_val == b_item.int_val) ? COMP_TRUE : COMP_FALSE;
    }
    else if (a_item.type_id == LMD_TYPE_INT64) {
        return (*(long*)a_item.pointer == *(long*)b_item.pointer) ? COMP_TRUE : COMP_FALSE;
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT) {
        return (*(double*)a_item.pointer == *(double*)b_item.pointer) ? COMP_TRUE : COMP_FALSE;
    }
    else if (a_item.type_id == LMD_TYPE_STRING || a_item.type_id == LMD_TYPE_SYMBOL || 
        a_item.type_id == LMD_TYPE_BINARY || a_item.type_id == LMD_TYPE_DTIME) {
        String *str_a = (String*)a_item.pointer;  String *str_b = (String*)b_item.pointer;
        bool result = (str_a->len == str_b->len && strncmp(str_a->chars, str_b->chars, str_a->len) == 0);
        return result ? COMP_TRUE : COMP_FALSE;
    }
    printf("unknown comparing type %d\n", a_item.type_id);
    return COMP_ERROR;
#endif
}

// Comparison functions with fast path for int/float and fallback for other types

Item fn_eq(Item a_item, Item b_item) {
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_UTF8PROC
    // Use utf8proc Unicode-enhanced equality comparison
    return fn_eq_utf8proc(a_item, b_item);
#elif LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    // Use ICU Unicode-enhanced equality comparison (deprecated)
    return fn_eq_unicode(a_item, b_item);
#else
    // Fast path for numeric types
    if (a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_INT) {
        return {.item = b2it(a_item.int_val == b_item.int_val)};
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_FLOAT) {
        return {.item = b2it(*(double*)a_item.pointer == *(double*)b_item.pointer)};
    }
    else if ((a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_FLOAT) ||
             (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_INT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? (double)a_item.int_val : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? (double)b_item.int_val : *(double*)b_item.pointer;
        return {.item = b2it(a_val == b_val)};
    }
    else if (a_item.type_id == LMD_TYPE_BOOL && b_item.type_id == LMD_TYPE_BOOL) {
        return {.item = b2it(a_item.bool_val == b_item.bool_val)};
    }
    
    // Fallback to 3-state comparison function
    printf("fn_eq fallback\n");
    CompResult result = equal_comp(a_item, b_item);
    if (result == COMP_ERROR) {
        printf("equality type error for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    return {.item = b2it(result == COMP_TRUE)};
#endif
}

Item fn_ne(Item a_item, Item b_item) {
#if LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_UTF8PROC
    // Use utf8proc Unicode-enhanced inequality comparison
    return fn_ne_utf8proc(a_item, b_item);
#elif LAMBDA_UNICODE_LEVEL >= LAMBDA_UNICODE_COMPACT
    // Use ICU Unicode-enhanced inequality comparison (deprecated)
    return fn_ne_unicode(a_item, b_item);
#else
    // Fast path for numeric types
    if (a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_INT) {
        return {.item = b2it(a_item.int_val != b_item.int_val)};
    }
    else if (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_FLOAT) {
        return {.item = b2it(*(double*)a_item.pointer != *(double*)b_item.pointer)};
    }
    else if ((a_item.type_id == LMD_TYPE_INT && b_item.type_id == LMD_TYPE_FLOAT) ||
             (a_item.type_id == LMD_TYPE_FLOAT && b_item.type_id == LMD_TYPE_INT)) {
        double a_val = (a_item.type_id == LMD_TYPE_INT) ? (double)a_item.int_val : *(double*)a_item.pointer;
        double b_val = (b_item.type_id == LMD_TYPE_INT) ? (double)b_item.int_val : *(double*)b_item.pointer;
        return {.item = b2it(a_val != b_val)};
    }
    else if (a_item.type_id == LMD_TYPE_BOOL && b_item.type_id == LMD_TYPE_BOOL) {
        return {.item = b2it(a_item.bool_val != b_item.bool_val)};
    }
    
    // Fallback to 3-state comparison function
    printf("fn_ne fallback\n");
    CompResult result = equal_comp(a_item, b_item);
    if (result == COMP_ERROR) {
        printf("inequality type error for types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    return {.item = b2it(result == COMP_FALSE)};
#endif
}

Item fn_lt(Item a_item, Item b_item) {
    // Always use utf8proc Unicode-enhanced less-than comparison (supports string comparison)
    CompResult result = less_comp_unicode(a_item, b_item);
    if (result == COMP_ERROR) {
        return ItemError;
    }
    return {.item = b2it(result == COMP_TRUE)};
}

Item fn_gt(Item a_item, Item b_item) {
    // Always use utf8proc Unicode-enhanced greater-than comparison (supports string comparison)
    CompResult result = greater_comp_unicode(a_item, b_item);
    if (result == COMP_ERROR) return ItemError;
    return {.item = b2it(result == COMP_TRUE)};
}

Item fn_le(Item a_item, Item b_item) {
    // Always use utf8proc Unicode-enhanced less-than-or-equal comparison (supports string comparison)
    CompResult result = less_equal_comp_unicode(a_item, b_item);
    if (result == COMP_ERROR) return ItemError;
    return {.item = b2it(result == COMP_TRUE)};
}

Item fn_ge(Item a_item, Item b_item) {
    // Always use utf8proc Unicode-enhanced greater-than-or-equal comparison (supports string comparison)
    CompResult result = greater_equal_comp_unicode(a_item, b_item);
    if (result == COMP_ERROR) return ItemError;
    return {.item = b2it(result == COMP_TRUE)};
}

Item fn_not(Item item) {
    // Logical NOT - invert the truthiness of the item
    printf("fn_not expr\n");
    return {.item = b2it(!item_true(item))};
}

Item fn_and(Item a_item, Item b_item) {
    printf("fn_and called with types: %d, %d\n", a_item.type_id, b_item.type_id);
    
    // Fast path for boolean types
    if (a_item.type_id == LMD_TYPE_BOOL && b_item.type_id == LMD_TYPE_BOOL) {
        printf("fn_and: bool fast path\n");
        return {.item = b2it(a_item.bool_val && b_item.bool_val)};
    }
    
    // Type error for string operands in logical operations
    if (a_item.type_id == LMD_TYPE_STRING || b_item.type_id == LMD_TYPE_STRING) {
        printf("logical AND not supported with string types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    
    // Type error for null operands in logical operations
    if (a_item.type_id == LMD_TYPE_NULL || b_item.type_id == LMD_TYPE_NULL) {
        printf("logical AND not supported with null types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    
    // Fallback to generic truthiness evaluation for numeric and boolean combinations
    printf("fn_and: generic truthiness fallback\n");
    bool a_truth = item_true(a_item);
    bool b_truth = item_true(b_item);
    return {.item = b2it(a_truth && b_truth)};
}

Item fn_or(Item a_item, Item b_item) {
    // Fast path for boolean types
    if (a_item.type_id == LMD_TYPE_BOOL && b_item.type_id == LMD_TYPE_BOOL) {
        return {.item = b2it(a_item.bool_val || b_item.bool_val)};
    }
    
    // Type error for string operands in logical operations
    if (a_item.type_id == LMD_TYPE_STRING || b_item.type_id == LMD_TYPE_STRING) {
        printf("logical OR not supported with string types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    
    // Type error for null operands in logical operations
    if (a_item.type_id == LMD_TYPE_NULL || b_item.type_id == LMD_TYPE_NULL) {
        printf("logical OR not supported with null types: %d, %d\n", a_item.type_id, b_item.type_id);
        return ItemError;
    }
    
    // Fallback to generic truthiness evaluation for numeric and boolean combinations
    bool a_truth = item_true(a_item);
    bool b_truth = item_true(b_item);
    return {.item = b2it(a_truth || b_truth)};
}

bool fn_in(Item a_item, Item b_item) {
    printf("in expr\n");
    if (b_item.type_id) { // b is scalar
        if (b_item.type_id == LMD_TYPE_STRING && a_item.type_id == LMD_TYPE_STRING) {
            String *str_a = (String*)a_item.pointer;  String *str_b = (String*)b_item.pointer;
            return str_a->len <= str_b->len && strstr(str_b->chars, str_a->chars) != NULL;
        }
    }
    else { // b is container
        TypeId b_type = b_item.container->type_id;
        if (b_type == LMD_TYPE_LIST) {
            List *list = b_item.list;
            for (int i = 0; i < list->length; i++) {
                if (equal(list->items[i], a_item)) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_RANGE) {
            Range *range = b_item.range;
            int64_t a_val = it2l(a_item);
            return range->start <= a_val && a_val <= range->end;
        }
        else if (b_type == LMD_TYPE_ARRAY) {
            Array *arr = b_item.array;
            for (int i = 0; i < arr->length; i++) {
                if (equal(arr->items[i], a_item)) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_ARRAY_INT) {
            ArrayInt *arr = b_item.array_int;
            for (int i = 0; i < arr->length; i++) {
                if (arr->items[i] == it2l(a_item)) {
                    return true;
                }
            }
            return false;
        }
        else if (b_type == LMD_TYPE_MAP) {
            // check if a is in map
        }
        else if (b_type == LMD_TYPE_ELEMENT) {
            // check if a is in element
        }
        else {
            printf("invalid type %d\n", b_type);
        }
    }
    return false;
}

String STR_NULL = {.chars = "null", .len = 4};
String STR_TRUE = {.chars = "true", .len = 4};
String STR_FALSE = {.chars = "false", .len = 5};

String* fn_string(Item itm) {
    if (itm.type_id == LMD_TYPE_NULL) {
        return &STR_NULL;
    }
    else if (itm.type_id == LMD_TYPE_BOOL) {
        return itm.bool_val ? &STR_TRUE : &STR_FALSE;
    }    
    else if (itm.type_id == LMD_TYPE_STRING || itm.type_id == LMD_TYPE_SYMBOL || 
        itm.type_id == LMD_TYPE_BINARY) {
        return (String*)itm.pointer;
    }
    else if (itm.type_id == LMD_TYPE_DTIME) {
        DateTime *dt = (DateTime*)itm.pointer;
        if (dt) {
            // Debug: Print the datetime precision and basic info
            printf("fn_string debug: DateTime precision=%d, hour=%d, minute=%d, second=%d, ms=%d\n", 
                   dt->precision, dt->hour, dt->minute, dt->second, dt->millisecond);
            
            // Format datetime in Lambda format based on precision
            char buf[64];
            int len = 0;
            
            switch (dt->precision) {
                case DATETIME_PRECISION_YEAR_ONLY:
                    len = snprintf(buf, sizeof(buf), "t'%04d'", DATETIME_GET_YEAR(dt));
                    break;
                    
                case DATETIME_PRECISION_DATE_ONLY:
                    len = snprintf(buf, sizeof(buf), "t'%04d-%02d-%02d'", 
                        DATETIME_GET_YEAR(dt), DATETIME_GET_MONTH(dt), dt->day);
                    break;
                    
                case DATETIME_PRECISION_TIME_ONLY: {
                    // Debug: Print the datetime values we're formatting
                    printf("fn_string debug: formatting time-only: %02d:%02d:%02d.%03d, tz_offset=%d\n", 
                           dt->hour, dt->minute, dt->second, dt->millisecond, 
                           DATETIME_HAS_TIMEZONE(dt) ? DATETIME_GET_TZ_OFFSET(dt) : -999);
                    
                    // Format time-only without 'T' prefix
                    len = snprintf(buf, sizeof(buf), "t'%02d:%02d:%02d", 
                        dt->hour, dt->minute, dt->second);
                    
                    // Add milliseconds if non-zero
                    if (dt->millisecond > 0) {
                        len += snprintf(buf + len, sizeof(buf) - len, ".%03d", dt->millisecond);
                    }
                    
                    // Add timezone - use 'z' for UTC (+00:00)
                    if (DATETIME_HAS_TIMEZONE(dt)) {
                        int tz_offset = DATETIME_GET_TZ_OFFSET(dt);
                        if (tz_offset == 0) {
                            len += snprintf(buf + len, sizeof(buf) - len, "z");
                        } else {
                            int hours = abs(tz_offset) / 60;
                            int minutes = abs(tz_offset) % 60;
                            len += snprintf(buf + len, sizeof(buf) - len, "%+03d:%02d", 
                                tz_offset >= 0 ? hours : -hours, minutes);
                        }
                    }
                    
                    len += snprintf(buf + len, sizeof(buf) - len, "'");
                    break;
                }
                    
                case DATETIME_PRECISION_DATE_TIME:
                default: {
                    // Format full datetime with 'T' separator
                    len = snprintf(buf, sizeof(buf), "t'%04d-%02d-%02dT%02d:%02d:%02d", 
                        DATETIME_GET_YEAR(dt), DATETIME_GET_MONTH(dt), dt->day,
                        dt->hour, dt->minute, dt->second);
                    
                    // Add milliseconds if non-zero
                    if (dt->millisecond > 0) {
                        len += snprintf(buf + len, sizeof(buf) - len, ".%03d", dt->millisecond);
                    }
                    
                    // Add timezone - use 'z' for UTC (+00:00)
                    if (DATETIME_HAS_TIMEZONE(dt)) {
                        int tz_offset = DATETIME_GET_TZ_OFFSET(dt);
                        if (tz_offset == 0) {
                            len += snprintf(buf + len, sizeof(buf) - len, "z");
                        } else {
                            int hours = abs(tz_offset) / 60;
                            int minutes = abs(tz_offset) % 60;
                            len += snprintf(buf + len, sizeof(buf) - len, "%+03d:%02d", 
                                tz_offset >= 0 ? hours : -hours, minutes);
                        }
                    }
                    
                    len += snprintf(buf + len, sizeof(buf) - len, "'");
                    break;
                }
            }
            
            String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
            strcpy(str->chars, buf);
            str->len = len;  str->ref_cnt = 0;
            return str;
        } else {
            return &STR_NULL;
        }
    }
    else if (itm.type_id == LMD_TYPE_INT) {
        char buf[32];
        int int_val = itm.int_val;
        snprintf(buf, sizeof(buf), "%d", int_val);
        int len = strlen(buf);
        String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
        strcpy(str->chars, buf);
        str->len = len;  str->ref_cnt = 0;
        return (String*)str;
    }
    else if (itm.type_id == LMD_TYPE_INT64) {
        char buf[32];
        long long_val = *(long*)itm.pointer;
        snprintf(buf, sizeof(buf), "%ld", long_val);
        int len = strlen(buf);
        String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
        strcpy(str->chars, buf);
        str->len = len;  str->ref_cnt = 0;
        return (String*)str;
    }
    else if (itm.type_id == LMD_TYPE_FLOAT) {
        char buf[32];
        double dval = *(double*)itm.pointer;
        snprintf(buf, sizeof(buf), "%g", dval);
        int len = strlen(buf);
        String *str = (String *)heap_alloc(len + 1 + sizeof(String), LMD_TYPE_STRING);
        strcpy(str->chars, buf);
        str->len = len;  str->ref_cnt = 0;
        return (String*)str;
    }
    printf("unhandled type %d\n", itm.type_id);
    return NULL;
}

Type* base_type(TypeId type_id) {
    return (type_id <= 0 || type_id > LMD_TYPE_ERROR) ? 
        &LIT_TYPE_ERROR : ((TypeInfo*)context->type_info)[type_id].lit_type;
}

Type* const_type(int type_index) {
    ArrayList* type_list = (ArrayList*)context->type_list;
    if (type_index < 0 || type_index >= type_list->length) {
        return &LIT_TYPE_ERROR;
    }    
    Type* type = (Type*)(type_list->data[type_index]);
    printf("const_type %d, %d, %p\n", type_index, type->type_id, type);
    return type;
}

Type* fn_type(Item item) {
    TypeType *type = (TypeType *)calloc(1, sizeof(TypeType) + sizeof(Type)); 
    Type *item_type = (Type *)((uint8_t *)type + sizeof(TypeType));
    type->type = item_type;  type->type_id = LMD_TYPE_TYPE;
    if (item.type_id) {
        item_type->type_id = item.type_id;
    }
    else if (item.type_id == LMD_TYPE_RAW_POINTER) {
        item_type->type_id = item.container->type_id;
    }
    return (Type*)type;
}

Input* input_data(Context* ctx, String* url, String* type, String* flavor);

Item fn_input(Item url, Item type) {
    String* url_str;
    if (url.type_id != LMD_TYPE_STRING && url.type_id != LMD_TYPE_SYMBOL) {
        printf("input url must be a string or symbol, got type %d\n", url.type_id);
        return ItemNull;  // todo: push error
    }
    else {
        url_str = (String*)url.pointer;
    }
    
    String* type_str = NULL;
    String* flavor_str = NULL;
    
    TypeId type_id = get_type_id(type);
    if (type_id == LMD_TYPE_NULL) {
        // No type specified
        type_str = NULL;
    }
    else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        // Legacy behavior: type is a simple string/symbol
        type_str = (String*)type.pointer;
    }
    else if (type_id == LMD_TYPE_MAP) {
        printf("input type is a map\n");
        // New behavior: type is a map with options
        Map* options_map = type.map;
        
        // Extract 'type' from map
        bool is_found;
        Item input_type = _map_get((TypeMap*)options_map->type, options_map->data, "type", &is_found);
        if (!is_found || !input_type.item || input_type.type_id == LMD_TYPE_NULL) { // missing 'type' key
            type_str = NULL;
        } else {
            TypeId type_value_type = get_type_id(input_type);
            if (type_value_type == LMD_TYPE_STRING || type_value_type == LMD_TYPE_SYMBOL) {
                type_str = (String*)input_type.pointer;
            }
            else {
                printf("input type must be a string or symbol, got type %d\n", type_value_type);
                // todo: push error
                type_str = NULL;  // input type ignored
            }
        }

        // Extract 'flavor' from map
        Item input_flavor = _map_get((TypeMap*)options_map->type, options_map->data, "flavor", &is_found);
        if (!is_found || !input_flavor.item || input_flavor.type_id == LMD_TYPE_NULL) { // missing 'flavor' key
            flavor_str = NULL;
        } else {
            TypeId flavor_value_type = get_type_id(input_flavor);
            if (flavor_value_type == LMD_TYPE_STRING || flavor_value_type == LMD_TYPE_SYMBOL) {
                flavor_str = (String*)input_flavor.pointer;
            }
            else {
                printf("input flavor must be a string or symbol, got type %d\n", flavor_value_type);
                // todo: push error
                flavor_str = NULL;  // input flavor ignored
            }
        }
    }
    else {
        printf("input type must be a string, symbol, or map, got type %d\n", type_id);
        return ItemNull;  // todo: push error
    }
    
    Input *input = input_data(context, url_str, type_str, flavor_str);
    // todo: input should be cached in context
    return (input && input->root.item) ? input->root : ItemNull;
}

void fn_print(Item item) {
    String *str = fn_string(item);
    if (str) {
        printf("%s\n", str->chars);
    }
}

extern "C" String* format_data(Item item, String* type, String* flavor, VariableMemPool *pool);

// Single-argument format function wrapper - uses default formatting
extern "C" String* fn_format_simple(Item item) {
    return fn_format(item, ItemNull);
}

String* fn_format(Item item, Item type) {
    TypeId type_id = get_type_id(type);
    String* type_str = NULL;
    String* flavor_str = NULL;

    if (type_id == LMD_TYPE_NULL) {
        type_str = NULL;  // use default
    }
    else if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        // Legacy behavior: type is a simple string or symbol
        type_str = (String*)type.pointer;
    }
    else if (type_id == LMD_TYPE_MAP) {
        printf("format type is a map\n");
        // New behavior: type is a map with options
        Map* options_map = (Map*)type.pointer;
        
        // Extract 'type' from map
        bool is_found;
        Item format_type = _map_get((TypeMap*)options_map->type, options_map->data, "type", &is_found);
        if (!is_found || !format_type.item || format_type.type_id == LMD_TYPE_NULL) { // missing 'type' key
            type_str = NULL;
        } else {
            TypeId type_value_type = get_type_id(format_type);
            if (type_value_type == LMD_TYPE_STRING || type_value_type == LMD_TYPE_SYMBOL) {
                type_str = (String*)format_type.pointer;
            }
            else {
                printf("format type must be a string or symbol, got type %d\n", type_value_type);
                // todo: push error
                type_str = NULL;  // format type ignored
            }
        }

        // Extract 'flavor' from map
        Item format_flavor = _map_get((TypeMap*)options_map->type, options_map->data, "flavor", &is_found);
        if (!is_found || !format_flavor.item || format_flavor.type_id == LMD_TYPE_NULL) { // missing 'flavor' key
            flavor_str = NULL;
        } else {
            TypeId flavor_value_type = get_type_id(format_flavor);
            if (flavor_value_type == LMD_TYPE_STRING || flavor_value_type == LMD_TYPE_SYMBOL) {
                flavor_str = (String*)format_flavor.pointer;
            }
            else {
                printf("format flavor must be a string or symbol, got type %d\n", flavor_value_type);
                // todo: push error
                flavor_str = NULL;  // format flavor ignored
            }
        }
    }
    else {
        printf("format type must be a string, symbol, or map, got type %d\n", type_id);
        return NULL;  // todo: push error
    }
    
    // printf("format item type: %s, flavor: %s\n", type_str ? type_str->chars : "null", flavor_str ? flavor_str->chars : "null");
    String* result = format_data(item, type_str, flavor_str, context->heap->pool);
    if (result) {
         arraylist_append(context->heap->entries, (void*)s2it(result));
    }
    return result;
}

#include "../lib/utf.h"

// generic field access function for any type
Item fn_index(Item item, Item index_item) {
    // Determine the type and delegate to appropriate getter
    long index = -1;
    switch (index_item.type_id) {
    case LMD_TYPE_INT:
        index = index_item.int_val;
        break;
    case LMD_TYPE_INT64:
        index = *(long*)index_item.pointer;
        break;
    case LMD_TYPE_FLOAT: {
        double dval = *(double*)index_item.pointer;
        // check dval is an integer
        if (dval == (long)dval) {
            index = (long)dval;
        } else {
            printf("index must be an integer, got float %g\n", dval);
            return ItemNull;  // todo: push error
        }
        break;
    }
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:
        return fn_member(item, index_item);
    default:
        printf("invalid index type %d\n", index_item.type_id);
        return ItemNull;
    }

    printf("fn_index item index: %ld\n", index);
    TypeId type_id = get_type_id(item);
    switch (type_id) {
    case LMD_TYPE_RANGE: {
        Range *range = item.range;
        if (index < range->start || index > range->end) { return ItemNull; }
        long value = range->start + index;
        return {.item = i2it(value)};
    }
    case LMD_TYPE_ARRAY:
        return array_get(item.array, (int)index);
    case LMD_TYPE_ARRAY_INT: {
        ArrayInt *arr = item.array_int;
        if (index < 0 || index >= arr->length) { return ItemNull; }
        return {.item = i2it(arr->items[index])};
    }
    case LMD_TYPE_ARRAY_FLOAT: {
        ArrayFloat *arr = item.array_float;
        if (index < 0 || index >= arr->length) { return ItemNull; }
        return push_d(arr->items[index]);
    }
    case LMD_TYPE_LIST:
        return list_get(item.list, (int)index);
    case LMD_TYPE_ELEMENT:
        // treat element as list for index access
        return list_get(item.list, (int)index);
    case LMD_TYPE_MAP:
        // to consider: should we return ITEM_NULL or ITEM_ERROR? 
        return ItemNull;
    // todo: string, symbol, dtime, binary, etc.
    default:
        return ItemNull;
    }
}

Item fn_member(Item item, Item key) {
    TypeId type_id = get_type_id(item);
    switch (type_id) {
    case LMD_TYPE_MAP: {
        Map *map = item.map;
        return map_get(map, key);
    }
    case LMD_TYPE_ELEMENT: {
        Element *elmt = item.element;
        return elmt_get(elmt, key);
    }
    case LMD_TYPE_LIST: {
        // Handle built-in properties for List type
        if (key.type_id == LMD_TYPE_STRING || key.type_id == LMD_TYPE_SYMBOL) {
            String *key_str = (String*)key.pointer;
            if (key_str && strcmp(key_str->chars, "length") == 0) {
                List *list = item.list;
                return {.item = i2it(list->length)};
            }
        }
        return ItemNull;
    }
    // todo: built-in properties for other types
    default:
        return ItemNull;
    }
}

// length of an item's content, relates to indexed access, i.e. item[index] 
int64_t fn_len(Item item) {
    TypeId type_id = get_type_id(item);
    printf("fn_len item: %d\n", type_id);
    int64_t size = 0;
    switch (type_id) {
    case LMD_TYPE_LIST:
        size = item.list->length;
        break;
    case LMD_TYPE_RANGE:
        size = item.range->length;
        break;
    case LMD_TYPE_ARRAY:
        size = item.array->length;
        break;
    case LMD_TYPE_ARRAY_INT:
        size = item.array_int->length;
        break;
    case LMD_TYPE_ARRAY_INT64:
        size = item.array_int64->length;
        break;
    case LMD_TYPE_ARRAY_FLOAT:
        size = item.array_float->length;
        break;
    case LMD_TYPE_MAP: {
        size = 0;
        break;
    }
    case LMD_TYPE_ELEMENT: {
        Element *elmt = item.element;
        size = elmt->length;
        break;
    }
    case LMD_TYPE_STRING:  case LMD_TYPE_SYMBOL:  case LMD_TYPE_BINARY: {
        // returns the length of the string
        // todo: binary length
        String *str = (String*)item.pointer;  // todo:: should return char length
        size = str ? utf8_char_count(str->chars) : 0;
        break;
    }
    case LMD_TYPE_ERROR:
        return INT_ERROR;
    default: // NULL and scalar types
        size = 0;
        break;
    }
    return size;
}

// substring system function - extracts a substring from start to end (exclusive)
Item fn_substring(Item str_item, Item start_item, Item end_item) {
    if (get_type_id(str_item) != LMD_TYPE_STRING) {
        printf("fn_substring: first argument must be a string\n");
        return ItemError;
    }
    
    if (get_type_id(start_item) != LMD_TYPE_INT && get_type_id(start_item) != LMD_TYPE_INT64) {
        printf("fn_substring: start index must be an integer\n");
        return ItemError;
    }
    
    if (get_type_id(end_item) != LMD_TYPE_INT && get_type_id(end_item) != LMD_TYPE_INT64) {
        printf("fn_substring: end index must be an integer\n");
        return ItemError;
    }
    
    String* str = (String*)str_item.pointer;
    if (!str || str->len == 0) {
        return str_item; // return empty string
    }
    
    int64_t start = it2l(start_item);
    int64_t end = it2l(end_item);
    
    // handle negative indices (count from end)
    int64_t char_len = utf8_char_count(str->chars);
    if (start < 0) start = char_len + start;
    if (end < 0) end = char_len + end;
    
    // clamp to valid range
    if (start < 0) start = 0;
    if (end > char_len) end = char_len;
    if (start >= end) {
        // return empty string
        String* empty = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
        empty->len = 0;
        empty->chars[0] = '\0';
        return {.item = s2it(empty)};
    }
    
    // convert char indices to byte indices
    long byte_start = utf8_char_to_byte_offset(str->chars, start);
    long byte_end = utf8_char_to_byte_offset(str->chars, end);
    
    if (byte_start >= str->len || byte_end < 0) {
        // return empty string
        String* empty = (String *)heap_alloc(sizeof(String) + 1, LMD_TYPE_STRING);
        empty->len = 0;
        empty->chars[0] = '\0';
        return {.item = s2it(empty)};
    }
    
    long result_len = byte_end - byte_start;
    String* result = (String *)heap_alloc(sizeof(String) + result_len + 1, LMD_TYPE_STRING);
    result->len = result_len;
    memcpy(result->chars, str->chars + byte_start, result_len);
    result->chars[result_len] = '\0';
    
    return {.item = s2it(result)};
}

// contains system function - checks if a string contains a substring
Item fn_contains(Item str_item, Item substr_item) {
    if (get_type_id(str_item) != LMD_TYPE_STRING) {
        printf("fn_contains: first argument must be a string\n");
        return ItemError;
    }
    
    if (get_type_id(substr_item) != LMD_TYPE_STRING) {
        printf("fn_contains: second argument must be a string\n");
        return ItemError;
    }
    
    String* str = (String*)str_item.pointer;
    String* substr = (String*)substr_item.pointer;
    
    if (!str || !substr) {
        return {.item = b2it(false)};
    }
    
    if (substr->len == 0) {
        return {.item = b2it(true)}; // empty string is contained in any string
    }
    
    if (str->len == 0 || substr->len > str->len) {
        return {.item = b2it(false)};
    }
    
    // simple byte-based search for now - could be optimized with KMP or Boyer-Moore
    for (int i = 0; i <= str->len - substr->len; i++) {
        if (memcmp(str->chars + i, substr->chars, substr->len) == 0) {
            return {.item = b2it(true)};
        }
    }
    
    return {.item = b2it(false)};
}

// Static DateTime instance to avoid dynamic allocation issues
static DateTime static_dt;
static bool static_dt_initialized = false;

// DateTime system function - creates a current DateTime
DateTime fn_datetime() {
    // Use a static DateTime to avoid heap allocation issues - this is not roubust, not thread-safe
    if (!static_dt_initialized) {
        memset(&static_dt, 0, sizeof(DateTime));
        static_dt_initialized = true;
    }
    
    // Get current time
    time_t now = time(NULL);
    struct tm* tm_utc = gmtime(&now);
    if (tm_utc) {
        // Set date and time from current UTC time
        DATETIME_SET_YEAR_MONTH(&static_dt, tm_utc->tm_year + 1900, tm_utc->tm_mon + 1);
        static_dt.day = tm_utc->tm_mday;
        static_dt.hour = tm_utc->tm_hour;
        static_dt.minute = tm_utc->tm_min;
        static_dt.second = tm_utc->tm_sec;
        static_dt.millisecond = 0;
    }
    
    // Set as UTC timezone
    DATETIME_SET_TZ_OFFSET(&static_dt, 0);
    static_dt.precision = DATETIME_PRECISION_DATE_TIME;
    static_dt.format_hint = DATETIME_FORMAT_ISO8601_UTC;
    
    return static_dt;
}

// ArrayFloat runtime functions

ArrayFloat* array_float(int count, ...) {
    if (count <= 0) { return NULL; }
    va_list args;
    va_start(args, count);
    ArrayFloat *arr = (ArrayFloat*)heap_alloc(sizeof(ArrayFloat), LMD_TYPE_ARRAY_FLOAT);
    arr->type_id = LMD_TYPE_ARRAY_FLOAT;
    arr->capacity = count;
    arr->items = (double*)Malloc(count * sizeof(double));
    arr->length = count;
    for (int i = 0; i < count; i++) {
        arr->items[i] = va_arg(args, double);
    }       
    va_end(args);
    return arr;
}

// Wrapper function to return an Item instead of raw ArrayFloat*
Item array_float_item(int count, ...) {
    if (count <= 0) { 
        ArrayFloat *arr = (ArrayFloat*)heap_alloc(sizeof(ArrayFloat), LMD_TYPE_ARRAY_FLOAT);
        arr->type_id = LMD_TYPE_ARRAY_FLOAT;
        arr->capacity = 0;
        arr->items = NULL;
        arr->length = 0;
        return (Item){.array_float = arr};
    }
    va_list args;
    va_start(args, count);
    ArrayFloat *arr = (ArrayFloat*)heap_alloc(sizeof(ArrayFloat), LMD_TYPE_ARRAY_FLOAT);
    arr->type_id = LMD_TYPE_ARRAY_FLOAT;  
    arr->capacity = count;
    arr->items = (double*)Malloc(count * sizeof(double));
    arr->length = count;
    for (int i = 0; i < count; i++) {
        arr->items[i] = va_arg(args, double);
    }       
    va_end(args);
    return (Item){.array_float = arr};
}

double array_float_get(ArrayFloat *arr, int index) {
    if (!arr || index < 0 || index >= arr->length) {
        return 0.0;  // Return 0.0 for invalid access
    }
    return arr->items[index];
}

void array_float_set(ArrayFloat *arr, int index, double value) {
    if (!arr || index < 0 || index >= arr->capacity) {
        return;  // Invalid access, do nothing
    }
    arr->items[index] = value;
    // Update length if we're setting beyond current length
    if (index >= arr->length) {
        arr->length = index + 1;
    }
}

Item array_float_get_item(ArrayFloat *arr, int index) {
    if (!arr || index < 0 || index >= arr->length) {
        return ItemNull;  // Return null for invalid access
    }
    return push_d(arr->items[index]);
}

void array_float_set_item(ArrayFloat *arr, int index, Item value) {
    if (!arr || index < 0 || index >= arr->capacity) {
        return;  // Invalid access, do nothing
    }
    
    double dval = 0.0;
    TypeId type_id = get_type_id(value);
    
    // Convert item to double based on its type
    switch (type_id) {
        case LMD_TYPE_FLOAT:
            dval = *(double*)value.pointer;
            break;
        case LMD_TYPE_INT64:
            dval = (double)(*(long*)value.pointer);
            break;
        case LMD_TYPE_INT:
            dval = (double)(value.int_val);
            break;
        default:
            return;  // Unsupported type, do nothing
    }
    
    arr->items[index] = dval;
    // Update length if we're setting beyond current length
    if (index >= arr->length) {
        arr->length = index + 1;
    }
}

