// lambda-vector.cpp - Vectorized numeric operations for Lambda
// Implements element-wise arithmetic between scalars, arrays, lists, and ranges

#include "transpiler.hpp"
#include "../lib/log.h"
#include <cmath>

extern __thread EvalContext* context;

// Forward declarations from lambda-eval-num.cpp
Item push_d(double val);
Item push_l(int64_t val);

//==============================================================================
// Type Detection Helpers
//==============================================================================

// check if type is a scalar numeric type
static inline bool is_scalar_numeric(TypeId type) {
    return type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || 
           type == LMD_TYPE_FLOAT || type == LMD_TYPE_DECIMAL;
}

// check if type is a collection that supports vectorized operations
static inline bool is_vector_type(TypeId type) {
    return type == LMD_TYPE_ARRAY_INT || type == LMD_TYPE_ARRAY_INT64 ||
           type == LMD_TYPE_ARRAY_FLOAT || type == LMD_TYPE_ARRAY ||
           type == LMD_TYPE_LIST || type == LMD_TYPE_RANGE;
}

// check if type is a homogeneous numeric array
static inline bool is_homogeneous_array(TypeId type) {
    return type == LMD_TYPE_ARRAY_INT || type == LMD_TYPE_ARRAY_INT64 ||
           type == LMD_TYPE_ARRAY_FLOAT;
}

// check if type is any array type (homogeneous or heterogeneous)
static inline bool is_array_type(TypeId type) {
    return type == LMD_TYPE_ARRAY_INT || type == LMD_TYPE_ARRAY_INT64 ||
           type == LMD_TYPE_ARRAY_FLOAT || type == LMD_TYPE_ARRAY;
}

// get length of a vector-like item
static int64_t vector_length(Item item) {
    TypeId type = get_type_id(item);
    switch (type) {
        case LMD_TYPE_ARRAY_INT:   return item.array_int->length;
        case LMD_TYPE_ARRAY_INT64: return item.array_int64->length;
        case LMD_TYPE_ARRAY_FLOAT: return item.array_float->length;
        case LMD_TYPE_ARRAY:
        case LMD_TYPE_LIST:        return item.list->length;
        case LMD_TYPE_RANGE:       return item.range->length;
        default:                   return -1;
    }
}

// get element from a vector-like item at index
static Item vector_get(Item item, int64_t index) {
    TypeId type = get_type_id(item);
    switch (type) {
        case LMD_TYPE_ARRAY_INT:
            return { .item = i2it(item.array_int->items[index]) };
        case LMD_TYPE_ARRAY_INT64:
            return push_l(item.array_int64->items[index]);
        case LMD_TYPE_ARRAY_FLOAT:
            return push_d(item.array_float->items[index]);
        case LMD_TYPE_ARRAY:
        case LMD_TYPE_LIST:
            return item.list->items[index];
        case LMD_TYPE_RANGE:
            return { .item = i2it(item.range->start + index) };
        default:
            return ItemError;
    }
}

// convert item to double for arithmetic (returns NAN on error)
static double item_to_double(Item item) {
    TypeId type = get_type_id(item);
    switch (type) {
        case LMD_TYPE_INT:   return (double)item.get_int56();
        case LMD_TYPE_INT64: return (double)item.get_int64();
        case LMD_TYPE_FLOAT: return item.get_double();
        default:             return NAN;
    }
}

// check if result should be float (any operand is float)
static bool needs_float_result(TypeId a, TypeId b) {
    return a == LMD_TYPE_FLOAT || b == LMD_TYPE_FLOAT ||
           a == LMD_TYPE_ARRAY_FLOAT || b == LMD_TYPE_ARRAY_FLOAT;
}

//==============================================================================
// Vector-Scalar Operations (scalar op vector, or vector op scalar)
//==============================================================================

// Generic template for scalar-vector binary operation
// op: 0=add, 1=sub, 2=mul, 3=div, 4=mod, 5=pow
static Item vec_scalar_op(Item vec, Item scalar, int op, bool scalar_first) {
    int64_t len = vector_length(vec);
    TypeId vec_type = get_type_id(vec);
    if (len < 0) return ItemError;
    if (len == 0) {
        // return empty array/list matching input type
        if (is_array_type(vec_type)) {
            return { .array = array() };
        }
        return { .list = list() };
    }

    TypeId scalar_type = get_type_id(scalar);
    double scalar_val = item_to_double(scalar);
    
    if (std::isnan(scalar_val)) {
        log_error("vec_scalar_op: non-numeric scalar type %s", get_type_name(scalar_type));
        return ItemError;
    }

    // determine result type
    bool use_float = needs_float_result(vec_type, scalar_type) || op == 3; // div always float

    // fast path for homogeneous arrays
    if (vec_type == LMD_TYPE_ARRAY_INT64 && !use_float && op != 3 && op != 5) {
        ArrayInt64* arr = vec.array_int64;
        int64_t sval = (int64_t)scalar_val;
        ArrayInt64* result = array_int64_new(len);
        for (int64_t i = 0; i < len; i++) {
            int64_t elem = arr->items[i];
            switch (op) {
                case 0: result->items[i] = scalar_first ? sval + elem : elem + sval; break;
                case 1: result->items[i] = scalar_first ? sval - elem : elem - sval; break;
                case 2: result->items[i] = scalar_first ? sval * elem : elem * sval; break;
                case 4: result->items[i] = scalar_first ? (elem != 0 ? sval % elem : 0) 
                                                        : (sval != 0 ? elem % sval : 0); break;
                default: result->items[i] = elem; break;
            }
        }
        return { .array_int64 = result };
    }

    if (vec_type == LMD_TYPE_ARRAY_FLOAT || use_float) {
        ArrayFloat* result = array_float_new(len);
        for (int64_t i = 0; i < len; i++) {
            Item elem = vector_get(vec, i);
            double elem_val = item_to_double(elem);
            double res;
            if (std::isnan(elem_val)) {
                // non-numeric element: store NAN as error indicator
                res = NAN;
            } else {
                double a = scalar_first ? scalar_val : elem_val;
                double b = scalar_first ? elem_val : scalar_val;
                switch (op) {
                    case 0: res = a + b; break;
                    case 1: res = a - b; break;
                    case 2: res = a * b; break;
                    case 3: res = a / b; break;  // div by zero -> inf
                    case 4: res = fmod(a, b); break;
                    case 5: res = pow(a, b); break;
                    default: res = NAN; break;
                }
            }
            result->items[i] = res;
        }
        return { .array_float = result };
    }

    // heterogeneous: element-wise with ERROR for non-numeric
    // preserve array type if input was array
    bool return_array = is_array_type(vec_type);
    Array* arr_result = return_array ? array() : nullptr;
    List* list_result = return_array ? nullptr : list();
    
    for (int64_t i = 0; i < len; i++) {
        Item elem = vector_get(vec, i);
        TypeId elem_type = get_type_id(elem);
        
        if (!is_scalar_numeric(elem_type)) {
            // non-numeric element: produce ERROR, continue
            if (return_array) array_push(arr_result, ItemError);
            else list_push(list_result, ItemError);
            continue;
        }
        
        double elem_val = item_to_double(elem);
        double a = scalar_first ? scalar_val : elem_val;
        double b = scalar_first ? elem_val : scalar_val;
        double res;
        
        switch (op) {
            case 0: res = a + b; break;
            case 1: res = a - b; break;
            case 2: res = a * b; break;
            case 3: res = a / b; break;
            case 4: res = fmod(a, b); break;
            case 5: res = pow(a, b); break;
            default: res = NAN; break;
        }
        
        // try to preserve integer type if possible
        Item res_item;
        if (scalar_type != LMD_TYPE_FLOAT && elem_type != LMD_TYPE_FLOAT && 
            op != 3 && op != 5 && res == (int64_t)res) {
            res_item = { .item = i2it((int64_t)res) };
        } else {
            res_item = push_d(res);
        }
        
        if (return_array) array_push(arr_result, res_item);
        else list_push(list_result, res_item);
    }
    return return_array ? Item{ .array = arr_result } : Item{ .list = list_result };
}

//==============================================================================
// Vector-Vector Operations
//==============================================================================

static Item vec_vec_op(Item vec_a, Item vec_b, int op) {
    int64_t len_a = vector_length(vec_a);
    int64_t len_b = vector_length(vec_b);
    TypeId type_a = get_type_id(vec_a);
    TypeId type_b = get_type_id(vec_b);
    
    if (len_a < 0 || len_b < 0) return ItemError;
    
    // empty vectors - preserve array type if either was array
    if (len_a == 0 || len_b == 0) {
        if (is_array_type(type_a) || is_array_type(type_b)) {
            return { .array = array() };
        }
        return { .list = list() };
    }
    
    // single-element broadcasting
    if (len_a == 1 && len_b > 1) {
        Item scalar = vector_get(vec_a, 0);
        return vec_scalar_op(vec_b, scalar, op, true);
    }
    if (len_b == 1 && len_a > 1) {
        Item scalar = vector_get(vec_b, 0);
        return vec_scalar_op(vec_a, scalar, op, false);
    }
    
    // size mismatch
    if (len_a != len_b) {
        log_error("vector size mismatch: %ld vs %ld", len_a, len_b);
        return ItemError;
    }
    
    int64_t len = len_a;
    bool use_float = needs_float_result(type_a, type_b) || op == 3;
    
    // fast path: both ArrayInt64
    if (type_a == LMD_TYPE_ARRAY_INT64 && type_b == LMD_TYPE_ARRAY_INT64 && 
        !use_float && op != 3 && op != 5) {
        ArrayInt64* a = vec_a.array_int64;
        ArrayInt64* b = vec_b.array_int64;
        ArrayInt64* result = array_int64_new(len);
        for (int64_t i = 0; i < len; i++) {
            switch (op) {
                case 0: result->items[i] = a->items[i] + b->items[i]; break;
                case 1: result->items[i] = a->items[i] - b->items[i]; break;
                case 2: result->items[i] = a->items[i] * b->items[i]; break;
                case 4: result->items[i] = b->items[i] != 0 ? a->items[i] % b->items[i] : 0; break;
                default: result->items[i] = a->items[i]; break;
            }
        }
        return { .array_int64 = result };
    }
    
    // fast path: both ArrayFloat or need float result
    if ((type_a == LMD_TYPE_ARRAY_FLOAT && type_b == LMD_TYPE_ARRAY_FLOAT) ||
        (is_homogeneous_array(type_a) && is_homogeneous_array(type_b) && use_float)) {
        ArrayFloat* result = array_float_new(len);
        for (int64_t i = 0; i < len; i++) {
            double a = item_to_double(vector_get(vec_a, i));
            double b = item_to_double(vector_get(vec_b, i));
            switch (op) {
                case 0: result->items[i] = a + b; break;
                case 1: result->items[i] = a - b; break;
                case 2: result->items[i] = a * b; break;
                case 3: result->items[i] = a / b; break;
                case 4: result->items[i] = fmod(a, b); break;
                case 5: result->items[i] = pow(a, b); break;
                default: result->items[i] = a; break;
            }
        }
        return { .array_float = result };
    }
    
    // heterogeneous: element-wise with ERROR for non-numeric
    // preserve array type if either input was array
    bool return_array = is_array_type(type_a) || is_array_type(type_b);
    Array* arr_result = return_array ? array() : nullptr;
    List* list_result = return_array ? nullptr : list();
    
    for (int64_t i = 0; i < len; i++) {
        Item elem_a = vector_get(vec_a, i);
        Item elem_b = vector_get(vec_b, i);
        TypeId ta = get_type_id(elem_a);
        TypeId tb = get_type_id(elem_b);
        
        if (!is_scalar_numeric(ta) || !is_scalar_numeric(tb)) {
            if (return_array) array_push(arr_result, ItemError);
            else list_push(list_result, ItemError);
            continue;
        }
        
        double a = item_to_double(elem_a);
        double b = item_to_double(elem_b);
        double res;
        
        switch (op) {
            case 0: res = a + b; break;
            case 1: res = a - b; break;
            case 2: res = a * b; break;
            case 3: res = a / b; break;
            case 4: res = fmod(a, b); break;
            case 5: res = pow(a, b); break;
            default: res = NAN; break;
        }
        
        Item res_item;
        if (ta != LMD_TYPE_FLOAT && tb != LMD_TYPE_FLOAT && 
            op != 3 && op != 5 && res == (int64_t)res) {
            res_item = { .item = i2it((int64_t)res) };
        } else {
            res_item = push_d(res);
        }
        
        if (return_array) array_push(arr_result, res_item);
        else list_push(list_result, res_item);
    }
    return return_array ? Item{ .array = arr_result } : Item{ .list = list_result };
}

//==============================================================================
// Public API: Vectorized Operations
// These are called from fn_add, fn_sub, etc. when vector types are detected
//==============================================================================

Item vec_add(Item a, Item b) {
    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    
    if (is_scalar_numeric(ta) && is_vector_type(tb)) {
        return vec_scalar_op(b, a, 0, true);
    }
    if (is_vector_type(ta) && is_scalar_numeric(tb)) {
        return vec_scalar_op(a, b, 0, false);
    }
    if (is_vector_type(ta) && is_vector_type(tb)) {
        return vec_vec_op(a, b, 0);
    }
    return ItemError;
}

Item vec_sub(Item a, Item b) {
    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    
    if (is_scalar_numeric(ta) && is_vector_type(tb)) {
        return vec_scalar_op(b, a, 1, true);
    }
    if (is_vector_type(ta) && is_scalar_numeric(tb)) {
        return vec_scalar_op(a, b, 1, false);
    }
    if (is_vector_type(ta) && is_vector_type(tb)) {
        return vec_vec_op(a, b, 1);
    }
    return ItemError;
}

Item vec_mul(Item a, Item b) {
    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    
    if (is_scalar_numeric(ta) && is_vector_type(tb)) {
        return vec_scalar_op(b, a, 2, true);
    }
    if (is_vector_type(ta) && is_scalar_numeric(tb)) {
        return vec_scalar_op(a, b, 2, false);
    }
    if (is_vector_type(ta) && is_vector_type(tb)) {
        return vec_vec_op(a, b, 2);
    }
    return ItemError;
}

Item vec_div(Item a, Item b) {
    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    
    if (is_scalar_numeric(ta) && is_vector_type(tb)) {
        return vec_scalar_op(b, a, 3, true);
    }
    if (is_vector_type(ta) && is_scalar_numeric(tb)) {
        return vec_scalar_op(a, b, 3, false);
    }
    if (is_vector_type(ta) && is_vector_type(tb)) {
        return vec_vec_op(a, b, 3);
    }
    return ItemError;
}

Item vec_mod(Item a, Item b) {
    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    
    if (is_scalar_numeric(ta) && is_vector_type(tb)) {
        return vec_scalar_op(b, a, 4, true);
    }
    if (is_vector_type(ta) && is_scalar_numeric(tb)) {
        return vec_scalar_op(a, b, 4, false);
    }
    if (is_vector_type(ta) && is_vector_type(tb)) {
        return vec_vec_op(a, b, 4);
    }
    return ItemError;
}

Item vec_pow(Item a, Item b) {
    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    
    if (is_scalar_numeric(ta) && is_vector_type(tb)) {
        return vec_scalar_op(b, a, 5, true);
    }
    if (is_vector_type(ta) && is_scalar_numeric(tb)) {
        return vec_scalar_op(a, b, 5, false);
    }
    if (is_vector_type(ta) && is_vector_type(tb)) {
        return vec_vec_op(a, b, 5);
    }
    return ItemError;
}

//==============================================================================
// Vector System Functions
//==============================================================================

// prod(vec) - product of all elements
Item fn_prod(Item item) {
    TypeId type = get_type_id(item);
    log_debug("fn_prod: type=%d", type);
    
    if (type == LMD_TYPE_ARRAY_INT) {
        ArrayInt* arr = item.array_int;
        if (arr->length == 0) return { .item = i2it(1) };
        int64_t prod = 1;
        for (int64_t i = 0; i < arr->length; i++) {
            prod *= arr->items[i];
        }
        return push_l(prod);
    }
    else if (type == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr = item.array_int64;
        if (arr->length == 0) return { .item = i2it(1) };
        int64_t prod = 1;
        for (int64_t i = 0; i < arr->length; i++) {
            prod *= arr->items[i];
        }
        return push_l(prod);
    }
    else if (type == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr = item.array_float;
        if (arr->length == 0) return push_d(1.0);
        double prod = 1.0;
        for (int64_t i = 0; i < arr->length; i++) {
            prod *= arr->items[i];
        }
        return push_d(prod);
    }
    else if (type == LMD_TYPE_ARRAY || type == LMD_TYPE_LIST) {
        List* lst = item.list;
        if (!lst || lst->length == 0) return { .item = i2it(1) };
        double prod = 1.0;
        bool has_float = false;
        for (int64_t i = 0; i < lst->length; i++) {
            Item elem = lst->items[i];
            double val = item_to_double(elem);
            if (std::isnan(val)) {
                log_error("fn_prod: non-numeric element at index %ld", i);
                return ItemError;
            }
            prod *= val;
            if (get_type_id(elem) == LMD_TYPE_FLOAT) has_float = true;
        }
        if (has_float) return push_d(prod);
        return { .item = i2it((int64_t)prod) };
    }
    else if (type == LMD_TYPE_RANGE) {
        Range* r = item.range;
        if (r->length == 0) return { .item = i2it(1) };
        int64_t prod = 1;
        for (int64_t i = r->start; i <= r->end; i++) {
            prod *= i;
        }
        return push_l(prod);
    }
    
    log_error("fn_prod: unsupported type %s", get_type_name(type));
    return ItemError;
}

// cumsum(vec) - cumulative sum
Item fn_cumsum(Item item) {
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) {
        List* result = list();
        return { .list = result };
    }
    
    TypeId type = get_type_id(item);
    
    if (type == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr = item.array_int64;
        ArrayInt64* result = array_int64_new(len);
        int64_t sum = 0;
        for (int64_t i = 0; i < len; i++) {
            sum += arr->items[i];
            result->items[i] = sum;
        }
        return { .array_int64 = result };
    }
    else if (type == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr = item.array_float;
        ArrayFloat* result = array_float_new(len);
        double sum = 0.0;
        for (int64_t i = 0; i < len; i++) {
            sum += arr->items[i];
            result->items[i] = sum;
        }
        return { .array_float = result };
    }
    else {
        // heterogeneous list
        ArrayFloat* result = array_float_new(len);
        double sum = 0.0;
        for (int64_t i = 0; i < len; i++) {
            Item elem = vector_get(item, i);
            double val = item_to_double(elem);
            if (std::isnan(val)) {
                result->items[i] = NAN;
            } else {
                sum += val;
                result->items[i] = sum;
            }
        }
        return { .array_float = result };
    }
}

// cumprod(vec) - cumulative product
Item fn_cumprod(Item item) {
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) {
        List* result = list();
        return { .list = result };
    }
    
    TypeId type = get_type_id(item);
    
    if (type == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr = item.array_int64;
        ArrayInt64* result = array_int64_new(len);
        int64_t prod = 1;
        for (int64_t i = 0; i < len; i++) {
            prod *= arr->items[i];
            result->items[i] = prod;
        }
        return { .array_int64 = result };
    }
    else if (type == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr = item.array_float;
        ArrayFloat* result = array_float_new(len);
        double prod = 1.0;
        for (int64_t i = 0; i < len; i++) {
            prod *= arr->items[i];
            result->items[i] = prod;
        }
        return { .array_float = result };
    }
    else {
        ArrayFloat* result = array_float_new(len);
        double prod = 1.0;
        for (int64_t i = 0; i < len; i++) {
            Item elem = vector_get(item, i);
            double val = item_to_double(elem);
            if (std::isnan(val)) {
                result->items[i] = NAN;
            } else {
                prod *= val;
                result->items[i] = prod;
            }
        }
        return { .array_float = result };
    }
}

// argmin(vec) - index of minimum element
Item fn_argmin(Item item) {
    int64_t len = vector_length(item);
    if (len <= 0) return ItemError;
    
    int64_t min_idx = 0;
    double min_val = item_to_double(vector_get(item, 0));
    
    for (int64_t i = 1; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (!std::isnan(val) && (std::isnan(min_val) || val < min_val)) {
            min_val = val;
            min_idx = i;
        }
    }
    
    return { .item = i2it(min_idx) };
}

// argmax(vec) - index of maximum element
Item fn_argmax(Item item) {
    int64_t len = vector_length(item);
    if (len <= 0) return ItemError;
    
    int64_t max_idx = 0;
    double max_val = item_to_double(vector_get(item, 0));
    
    for (int64_t i = 1; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (!std::isnan(val) && (std::isnan(max_val) || val > max_val)) {
            max_val = val;
            max_idx = i;
        }
    }
    
    return { .item = i2it(max_idx) };
}

// fill(n, value) - create vector of n copies of value
Item fn_fill(Item n_item, Item value) {
    if (get_type_id(n_item) != LMD_TYPE_INT && get_type_id(n_item) != LMD_TYPE_INT64) {
        log_error("fn_fill: first argument must be integer");
        return ItemError;
    }
    
    int64_t n = (get_type_id(n_item) == LMD_TYPE_INT) 
                ? n_item.get_int56() 
                : n_item.get_int64();
    
    if (n < 0) {
        log_error("fn_fill: count must be non-negative");
        return ItemError;
    }
    if (n == 0) {
        List* result = list();
        return { .list = result };
    }
    
    TypeId val_type = get_type_id(value);
    
    if (val_type == LMD_TYPE_INT || val_type == LMD_TYPE_INT64) {
        int64_t val = (val_type == LMD_TYPE_INT) ? value.get_int56() : value.get_int64();
        ArrayInt64* result = array_int64_new(n);
        for (int64_t i = 0; i < n; i++) {
            result->items[i] = val;
        }
        return { .array_int64 = result };
    }
    else if (val_type == LMD_TYPE_FLOAT) {
        double val = value.get_double();
        ArrayFloat* result = array_float_new(n);
        for (int64_t i = 0; i < n; i++) {
            result->items[i] = val;
        }
        return { .array_float = result };
    }
    else {
        // generic list for non-numeric values
        List* result = list();
        for (int64_t i = 0; i < n; i++) {
            list_push(result, value);
        }
        return { .list = result };
    }
}

// dot(a, b) - dot product of two vectors
Item fn_dot(Item a, Item b) {
    int64_t len_a = vector_length(a);
    int64_t len_b = vector_length(b);
    
    if (len_a != len_b || len_a < 0) {
        log_error("fn_dot: vectors must have same length");
        return ItemError;
    }
    
    if (len_a == 0) return push_d(0.0);
    
    double sum = 0.0;
    for (int64_t i = 0; i < len_a; i++) {
        double va = item_to_double(vector_get(a, i));
        double vb = item_to_double(vector_get(b, i));
        if (std::isnan(va) || std::isnan(vb)) {
            log_error("fn_dot: non-numeric element at index %ld", i);
            return ItemError;
        }
        sum += va * vb;
    }
    
    return push_d(sum);
}

// norm(vec) - Euclidean norm (L2 norm)
Item fn_norm(Item item) {
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) return push_d(0.0);
    
    double sum_sq = 0.0;
    for (int64_t i = 0; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (std::isnan(val)) {
            log_error("fn_norm: non-numeric element at index %ld", i);
            return ItemError;
        }
        sum_sq += val * val;
    }
    
    return push_d(sqrt(sum_sq));
}

//==============================================================================
// Statistical Functions
//==============================================================================

// mean(vec) - arithmetic mean (alias for avg)
Item fn_mean(Item item) {
    // Delegate to existing fn_avg
    extern Item fn_avg(Item);
    return fn_avg(item);
}

// median(vec) - median value
Item fn_median(Item item) {
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) return ItemNull;
    
    // Copy values to sortable array
    ArrayFloat* sorted = array_float_new(len);
    for (int64_t i = 0; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (std::isnan(val)) {
            log_error("fn_median: non-numeric element at index %ld", i);
            return ItemError;
        }
        sorted->items[i] = val;
    }
    
    // Simple bubble sort (can optimize later)
    for (int64_t i = 0; i < len - 1; i++) {
        for (int64_t j = 0; j < len - i - 1; j++) {
            if (sorted->items[j] > sorted->items[j + 1]) {
                double tmp = sorted->items[j];
                sorted->items[j] = sorted->items[j + 1];
                sorted->items[j + 1] = tmp;
            }
        }
    }
    
    if (len % 2 == 1) {
        return push_d(sorted->items[len / 2]);
    } else {
        return push_d((sorted->items[len / 2 - 1] + sorted->items[len / 2]) / 2.0);
    }
}

// variance(vec) - population variance
Item fn_variance(Item item) {
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) return ItemNull;
    
    // Calculate mean
    double sum = 0.0;
    for (int64_t i = 0; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (std::isnan(val)) {
            log_error("fn_variance: non-numeric element at index %ld", i);
            return ItemError;
        }
        sum += val;
    }
    double mean = sum / len;
    
    // Calculate variance
    double var_sum = 0.0;
    for (int64_t i = 0; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        double diff = val - mean;
        var_sum += diff * diff;
    }
    
    return push_d(var_sum / len);
}

// deviation(vec) - population standard deviation
Item fn_deviation(Item item) {
    Item var_result = fn_variance(item);
    if (get_type_id(var_result) == LMD_TYPE_ERROR) return var_result;
    if (get_type_id(var_result) == LMD_TYPE_NULL) return var_result;
    
    double variance = var_result.get_double();
    return push_d(sqrt(variance));
}

// quantile(vec, p) - p-th quantile (0 <= p <= 1)
Item fn_quantile(Item item, Item p_item) {
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) return ItemNull;
    
    double p = item_to_double(p_item);
    if (std::isnan(p) || p < 0.0 || p > 1.0) {
        log_error("fn_quantile: p must be between 0 and 1");
        return ItemError;
    }
    
    // Copy values to sortable array
    ArrayFloat* sorted = array_float_new(len);
    for (int64_t i = 0; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (std::isnan(val)) {
            log_error("fn_quantile: non-numeric element at index %ld", i);
            return ItemError;
        }
        sorted->items[i] = val;
    }
    
    // Sort
    for (int64_t i = 0; i < len - 1; i++) {
        for (int64_t j = 0; j < len - i - 1; j++) {
            if (sorted->items[j] > sorted->items[j + 1]) {
                double tmp = sorted->items[j];
                sorted->items[j] = sorted->items[j + 1];
                sorted->items[j + 1] = tmp;
            }
        }
    }
    
    // Linear interpolation method (same as NumPy's default)
    double idx = p * (len - 1);
    int64_t lo = (int64_t)floor(idx);
    int64_t hi = (int64_t)ceil(idx);
    
    if (lo == hi || hi >= len) {
        return push_d(sorted->items[lo]);
    }
    
    double frac = idx - lo;
    return push_d(sorted->items[lo] * (1.0 - frac) + sorted->items[hi] * frac);
}

//==============================================================================
// Element-wise Math Functions
//==============================================================================

// Helper: apply unary math function element-wise
static Item vec_unary_math(Item item, double (*func)(double), const char* name) {
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) {
        ArrayFloat* result = array_float_new(0);
        return { .array_float = result };
    }
    
    ArrayFloat* result = array_float_new(len);
    for (int64_t i = 0; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (std::isnan(val)) {
            result->items[i] = NAN;  // propagate error as NaN
        } else {
            result->items[i] = func(val);
        }
    }
    return { .array_float = result };
}

//==============================================================================
// Pipe Operations
//==============================================================================

typedef Item (*PipeMapFn)(Item item, Item index);

// helper: create an Item from an integer index
static Item index_to_item(int64_t index) {
    return { .item = i2it((int)index) };
}

// fn_pipe_map: apply transform function to each element of a collection
// For arrays/lists/ranges: ~# is index, ~ is value
// For maps: ~# is key (as string), ~ is value
Item fn_pipe_map(Item collection, PipeMapFn transform) {
    TypeId type = get_type_id(collection);
    
    // scalar case: apply transform directly
    if (type != LMD_TYPE_ARRAY && type != LMD_TYPE_LIST && 
        type != LMD_TYPE_RANGE && type != LMD_TYPE_MAP &&
        type != LMD_TYPE_ARRAY_INT && type != LMD_TYPE_ARRAY_INT64 &&
        type != LMD_TYPE_ARRAY_FLOAT && type != LMD_TYPE_ELEMENT) {
        return transform(collection, ItemNull);
    }
    
    // map case: iterate over key-value pairs
    if (type == LMD_TYPE_MAP) {
        Map* mp = collection.map;
        List* result = list();
        
        // use item_keys to get the list of keys
        ArrayList* keys = item_keys(collection);
        if (keys) {
            for (int64_t i = 0; i < keys->length; i++) {
                String* key_str = (String*)keys->data[i];
                Item key_item = { .item = s2it(key_str) };
                Item value = map_get(mp, key_item);
                Item transformed = transform(value, key_item);
                list_push(result, transformed);
            }
            arraylist_free(keys);
        }
        return { .list = result };
    }
    
    // element case: iterate over children (content items, not attributes)
    if (type == LMD_TYPE_ELEMENT) {
        Element* elem = collection.element;
        List* result = list();
        
        // element content starts at items[0] (attributes are in separate data struct)
        for (int64_t i = 0; i < elem->length; i++) {
            Item child = elem->items[i];
            Item idx = index_to_item(i);
            Item transformed = transform(child, idx);
            list_push(result, transformed);
        }
        return { .list = result };
    }
    
    // collection case: iterate with index
    int64_t len = vector_length(collection);
    if (len < 0) return ItemError;
    
    List* result = list();
    for (int64_t i = 0; i < len; i++) {
        Item elem = vector_get(collection, i);
        Item idx = index_to_item(i);
        Item transformed = transform(elem, idx);
        list_push(result, transformed);
    }
    return { .list = result };
}

// fn_pipe_where: filter elements where predicate is truthy
Item fn_pipe_where(Item collection, PipeMapFn predicate) {
    TypeId type = get_type_id(collection);
    
    // scalar case: return collection if truthy, else null
    if (type != LMD_TYPE_ARRAY && type != LMD_TYPE_LIST && 
        type != LMD_TYPE_RANGE && type != LMD_TYPE_MAP &&
        type != LMD_TYPE_ARRAY_INT && type != LMD_TYPE_ARRAY_INT64 &&
        type != LMD_TYPE_ARRAY_FLOAT && type != LMD_TYPE_ELEMENT) {
        Item result = predicate(collection, ItemNull);
        if (is_truthy(result)) {
            return collection;
        }
        return ItemNull;
    }
    
    // map case: filter key-value pairs (return list of values that pass predicate)
    if (type == LMD_TYPE_MAP) {
        Map* mp = collection.map;
        List* result = list();
        
        ArrayList* keys = item_keys(collection);
        if (keys) {
            for (int64_t i = 0; i < keys->length; i++) {
                String* key_str = (String*)keys->data[i];
                Item key_item = { .item = s2it(key_str) };
                Item value = map_get(mp, key_item);
                Item pred_result = predicate(value, key_item);
                if (is_truthy(pred_result)) {
                    list_push(result, value);
                }
            }
            arraylist_free(keys);
        }
        return { .list = result };
    }
    
    // element case: filter children (content items, not attributes)
    if (type == LMD_TYPE_ELEMENT) {
        Element* elem = collection.element;
        List* result = list();
        
        for (int64_t i = 0; i < elem->length; i++) {
            Item child = elem->items[i];
            Item idx = index_to_item(i);
            Item pred_result = predicate(child, idx);
            if (is_truthy(pred_result)) {
                list_push(result, child);
            }
        }
        return { .list = result };
    }
    
    // collection case
    int64_t len = vector_length(collection);
    if (len < 0) return ItemError;
    
    List* result = list();
    for (int64_t i = 0; i < len; i++) {
        Item elem = vector_get(collection, i);
        Item idx = index_to_item(i);
        Item pred_result = predicate(elem, idx);
        if (is_truthy(pred_result)) {
            list_push(result, elem);
        }
    }
    return { .list = result };
}

// fn_pipe_call: pass collection as first argument to a function
// This handles the aggregate case where ~ is not used
Item fn_pipe_call(Item collection, Item func_or_result) {
    // If the right side is already evaluated (not using ~), 
    // we assume it's a function that should receive the collection
    // For now, we check if it's a callable
    TypeId type = get_type_id(func_or_result);
    
    if (type == LMD_TYPE_FUNC) {
        // call function with collection as first argument
        Function* fn = func_or_result.function;
        if (fn && fn->ptr) {
            return fn_call1(fn, collection);
        }
        return ItemError;
    }
    
    // if right side is not a function, return it as-is
    // (the transform was already computed without ~)
    return func_or_result;
}

// sqrt(vec) - element-wise square root
Item fn_sqrt(Item item) {
    // Check if scalar
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(sqrt(val));
    }
    return vec_unary_math(item, sqrt, "fn_sqrt");
}

// log(vec) - element-wise natural logarithm
Item fn_log(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(log(val));
    }
    return vec_unary_math(item, log, "fn_log");
}

// log10(vec) - element-wise base-10 logarithm
Item fn_log10(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(log10(val));
    }
    return vec_unary_math(item, log10, "fn_log10");
}

// exp(vec) - element-wise exponential
Item fn_exp(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(exp(val));
    }
    return vec_unary_math(item, exp, "fn_exp");
}

// sin(vec) - element-wise sine
Item fn_sin(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(sin(val));
    }
    return vec_unary_math(item, sin, "fn_sin");
}

// cos(vec) - element-wise cosine
Item fn_cos(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(cos(val));
    }
    return vec_unary_math(item, cos, "fn_cos");
}

// tan(vec) - element-wise tangent
Item fn_tan(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(tan(val));
    }
    return vec_unary_math(item, tan, "fn_tan");
}

// sign helper
static double sign_func(double x) {
    if (x > 0) return 1.0;
    if (x < 0) return -1.0;
    return 0.0;
}

// sign(vec) - element-wise sign (-1, 0, 1)
Item fn_sign(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        int64_t s = (val > 0) ? 1 : (val < 0) ? -1 : 0;
        return { .item = i2it(s) };
    }
    
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) {
        ArrayInt64* result = array_int64_new(0);
        return { .array_int64 = result };
    }
    
    ArrayInt64* result = array_int64_new(len);
    for (int64_t i = 0; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (std::isnan(val)) {
            result->items[i] = 0;  // treat NaN as 0
        } else {
            result->items[i] = (val > 0) ? 1 : (val < 0) ? -1 : 0;
        }
    }
    return { .array_int64 = result };
}

//==============================================================================
// Vector Manipulation Functions
//==============================================================================

// reverse(vec) - reverse order of elements
Item fn_reverse(Item item) {
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) {
        List* result = list();
        return { .list = result };
    }
    
    TypeId type = get_type_id(item);
    
    if (type == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr = item.array_int64;
        ArrayInt64* result = array_int64_new(len);
        for (int64_t i = 0; i < len; i++) {
            result->items[i] = arr->items[len - 1 - i];
        }
        return { .array_int64 = result };
    }
    else if (type == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr = item.array_float;
        ArrayFloat* result = array_float_new(len);
        for (int64_t i = 0; i < len; i++) {
            result->items[i] = arr->items[len - 1 - i];
        }
        return { .array_float = result };
    }
    else {
        List* result = list();
        for (int64_t i = len - 1; i >= 0; i--) {
            list_push(result, vector_get(item, i));
        }
        return { .list = result };
    }
}

// sort(vec) - sort in ascending order
Item fn_sort1(Item item) {
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) {
        List* result = list();
        return { .list = result };
    }
    
    TypeId type = get_type_id(item);
    
    if (type == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr = item.array_int64;
        ArrayInt64* result = array_int64_new(len);
        for (int64_t i = 0; i < len; i++) result->items[i] = arr->items[i];
        // Simple bubble sort
        for (int64_t i = 0; i < len - 1; i++) {
            for (int64_t j = 0; j < len - i - 1; j++) {
                if (result->items[j] > result->items[j + 1]) {
                    int64_t tmp = result->items[j];
                    result->items[j] = result->items[j + 1];
                    result->items[j + 1] = tmp;
                }
            }
        }
        return { .array_int64 = result };
    }
    else if (type == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr = item.array_float;
        ArrayFloat* result = array_float_new(len);
        for (int64_t i = 0; i < len; i++) result->items[i] = arr->items[i];
        for (int64_t i = 0; i < len - 1; i++) {
            for (int64_t j = 0; j < len - i - 1; j++) {
                if (result->items[j] > result->items[j + 1]) {
                    double tmp = result->items[j];
                    result->items[j] = result->items[j + 1];
                    result->items[j + 1] = tmp;
                }
            }
        }
        return { .array_float = result };
    }
    else {
        // For mixed types, convert to float and sort
        ArrayFloat* result = array_float_new(len);
        for (int64_t i = 0; i < len; i++) {
            result->items[i] = item_to_double(vector_get(item, i));
        }
        for (int64_t i = 0; i < len - 1; i++) {
            for (int64_t j = 0; j < len - i - 1; j++) {
                if (result->items[j] > result->items[j + 1]) {
                    double tmp = result->items[j];
                    result->items[j] = result->items[j + 1];
                    result->items[j + 1] = tmp;
                }
            }
        }
        return { .array_float = result };
    }
}

// sort(vec, direction) - sort with direction ('asc' or 'desc')
Item fn_sort2(Item item, Item dir_item) {
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) {
        List* result = list();
        return { .list = result };
    }
    
    // Parse direction - default to ascending
    bool descending = false;
    TypeId dir_type = get_type_id(dir_item);
    if (dir_type == LMD_TYPE_STRING) {
        String* str = dir_item.get_string();
        if (str && (strcmp(str->chars, "desc") == 0 || strcmp(str->chars, "descending") == 0)) {
            descending = true;
        }
    } else if (dir_type == LMD_TYPE_SYMBOL) {
        String* sym = dir_item.get_symbol();
        if (sym && (strcmp(sym->chars, "desc") == 0 || strcmp(sym->chars, "descending") == 0)) {
            descending = true;
        }
    }
    
    TypeId type = get_type_id(item);
    
    if (type == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* arr = item.array_int64;
        ArrayInt64* result = array_int64_new(len);
        for (int64_t i = 0; i < len; i++) result->items[i] = arr->items[i];
        for (int64_t i = 0; i < len - 1; i++) {
            for (int64_t j = 0; j < len - i - 1; j++) {
                bool swap = descending ? (result->items[j] < result->items[j + 1])
                                       : (result->items[j] > result->items[j + 1]);
                if (swap) {
                    int64_t tmp = result->items[j];
                    result->items[j] = result->items[j + 1];
                    result->items[j + 1] = tmp;
                }
            }
        }
        return { .array_int64 = result };
    }
    else if (type == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* arr = item.array_float;
        ArrayFloat* result = array_float_new(len);
        for (int64_t i = 0; i < len; i++) result->items[i] = arr->items[i];
        for (int64_t i = 0; i < len - 1; i++) {
            for (int64_t j = 0; j < len - i - 1; j++) {
                bool swap = descending ? (result->items[j] < result->items[j + 1])
                                       : (result->items[j] > result->items[j + 1]);
                if (swap) {
                    double tmp = result->items[j];
                    result->items[j] = result->items[j + 1];
                    result->items[j + 1] = tmp;
                }
            }
        }
        return { .array_float = result };
    }
    else {
        ArrayFloat* result = array_float_new(len);
        for (int64_t i = 0; i < len; i++) {
            result->items[i] = item_to_double(vector_get(item, i));
        }
        for (int64_t i = 0; i < len - 1; i++) {
            for (int64_t j = 0; j < len - i - 1; j++) {
                bool swap = descending ? (result->items[j] < result->items[j + 1])
                                       : (result->items[j] > result->items[j + 1]);
                if (swap) {
                    double tmp = result->items[j];
                    result->items[j] = result->items[j + 1];
                    result->items[j + 1] = tmp;
                }
            }
        }
        return { .array_float = result };
    }
}

// unique(vec) - remove duplicates
Item fn_unique(Item item) {
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) {
        List* result = list();
        return { .list = result };
    }
    
    List* result = list();
    for (int64_t i = 0; i < len; i++) {
        Item elem = vector_get(item, i);
        double elem_val = item_to_double(elem);
        
        // Check if already in result
        bool found = false;
        for (int64_t j = 0; j < result->length; j++) {
            double res_val = item_to_double(result->items[j]);
            if (elem_val == res_val || (std::isnan(elem_val) && std::isnan(res_val))) {
                found = true;
                break;
            }
        }
        if (!found) {
            list_push(result, elem);
        }
    }
    return { .list = result };
}

// concat(v1, v2) - concatenate two vectors
Item fn_concat(Item a, Item b) {
    int64_t len_a = vector_length(a);
    int64_t len_b = vector_length(b);
    
    if (len_a < 0 || len_b < 0) return ItemError;
    
    TypeId type_a = get_type_id(a);
    TypeId type_b = get_type_id(b);
    
    // If both are same homogeneous type, preserve it
    if (type_a == LMD_TYPE_ARRAY_INT64 && type_b == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* result = array_int64_new(len_a + len_b);
        for (int64_t i = 0; i < len_a; i++) result->items[i] = a.array_int64->items[i];
        for (int64_t i = 0; i < len_b; i++) result->items[len_a + i] = b.array_int64->items[i];
        return { .array_int64 = result };
    }
    else if (type_a == LMD_TYPE_ARRAY_FLOAT && type_b == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* result = array_float_new(len_a + len_b);
        for (int64_t i = 0; i < len_a; i++) result->items[i] = a.array_float->items[i];
        for (int64_t i = 0; i < len_b; i++) result->items[len_a + i] = b.array_float->items[i];
        return { .array_float = result };
    }
    else {
        // Generic list concatenation
        List* result = list();
        for (int64_t i = 0; i < len_a; i++) list_push(result, vector_get(a, i));
        for (int64_t i = 0; i < len_b; i++) list_push(result, vector_get(b, i));
        return { .list = result };
    }
}

// take(vec, n) - first n elements
Item fn_take(Item vec, Item n_item) {
    int64_t len = vector_length(vec);
    if (len < 0) return ItemError;
    
    TypeId n_type = get_type_id(n_item);
    if (n_type != LMD_TYPE_INT && n_type != LMD_TYPE_INT64) {
        log_error("fn_take: n must be integer");
        return ItemError;
    }
    
    int64_t n = (n_type == LMD_TYPE_INT) ? n_item.get_int56() : n_item.get_int64();
    if (n < 0) n = 0;
    if (n > len) n = len;
    
    TypeId type = get_type_id(vec);
    
    if (type == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* result = array_int64_new(n);
        for (int64_t i = 0; i < n; i++) result->items[i] = vec.array_int64->items[i];
        return { .array_int64 = result };
    }
    else if (type == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* result = array_float_new(n);
        for (int64_t i = 0; i < n; i++) result->items[i] = vec.array_float->items[i];
        return { .array_float = result };
    }
    else {
        List* result = list();
        for (int64_t i = 0; i < n; i++) list_push(result, vector_get(vec, i));
        return { .list = result };
    }
}

// drop(vec, n) - drop first n elements
Item fn_drop(Item vec, Item n_item) {
    int64_t len = vector_length(vec);
    if (len < 0) return ItemError;
    
    TypeId n_type = get_type_id(n_item);
    if (n_type != LMD_TYPE_INT && n_type != LMD_TYPE_INT64) {
        log_error("fn_drop: n must be integer");
        return ItemError;
    }
    
    int64_t n = (n_type == LMD_TYPE_INT) ? n_item.get_int56() : n_item.get_int64();
    if (n < 0) n = 0;
    if (n > len) n = len;
    
    int64_t new_len = len - n;
    TypeId type = get_type_id(vec);
    
    if (type == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* result = array_int64_new(new_len);
        for (int64_t i = 0; i < new_len; i++) result->items[i] = vec.array_int64->items[n + i];
        return { .array_int64 = result };
    }
    else if (type == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* result = array_float_new(new_len);
        for (int64_t i = 0; i < new_len; i++) result->items[i] = vec.array_float->items[n + i];
        return { .array_float = result };
    }
    else {
        List* result = list();
        for (int64_t i = n; i < len; i++) list_push(result, vector_get(vec, i));
        return { .list = result };
    }
}

// slice(vec, start, end) - extract elements from start (inclusive) to end (exclusive)
// Works for arrays, lists, and strings
Item fn_slice(Item vec, Item start_item, Item end_item) {
    TypeId type = get_type_id(vec);
    
    // Handle strings - delegate to fn_substring
    if (type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL) {
        return fn_substring(vec, start_item, end_item);
    }
    
    int64_t len = vector_length(vec);
    if (len < 0) return ItemError;
    
    TypeId start_type = get_type_id(start_item);
    TypeId end_type = get_type_id(end_item);
    if ((start_type != LMD_TYPE_INT && start_type != LMD_TYPE_INT64) ||
        (end_type != LMD_TYPE_INT && end_type != LMD_TYPE_INT64)) {
        log_error("fn_slice: start and end must be integers");
        return ItemError;
    }
    
    int64_t start = (start_type == LMD_TYPE_INT) ? start_item.get_int56() : start_item.get_int64();
    int64_t end = (end_type == LMD_TYPE_INT) ? end_item.get_int56() : end_item.get_int64();
    
    // Handle negative indices
    if (start < 0) start = len + start;
    if (end < 0) end = len + end;
    
    // Clamp indices to valid range
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start > end) start = end;
    
    int64_t new_len = end - start;
    
    if (type == LMD_TYPE_ARRAY_INT64) {
        ArrayInt64* result = array_int64_new(new_len);
        for (int64_t i = 0; i < new_len; i++) result->items[i] = vec.array_int64->items[start + i];
        return { .array_int64 = result };
    }
    else if (type == LMD_TYPE_ARRAY_FLOAT) {
        ArrayFloat* result = array_float_new(new_len);
        for (int64_t i = 0; i < new_len; i++) result->items[i] = vec.array_float->items[start + i];
        return { .array_float = result };
    }
    else {
        List* result = list();
        for (int64_t i = start; i < end; i++) list_push(result, vector_get(vec, i));
        return { .list = result };
    }
}

// zip(v1, v2) - pair elements into tuples
Item fn_zip(Item a, Item b) {
    int64_t len_a = vector_length(a);
    int64_t len_b = vector_length(b);
    
    if (len_a < 0 || len_b < 0) return ItemError;
    
    int64_t len = (len_a < len_b) ? len_a : len_b;  // take shorter length
    
    List* result = list();
    for (int64_t i = 0; i < len; i++) {
        List* pair = list();
        list_push(pair, vector_get(a, i));
        list_push(pair, vector_get(b, i));
        list_push(result, { .list = pair });
    }
    return { .list = result };
}

// range(start, end, step) - generate range with step
Item fn_range3(Item start_item, Item end_item, Item step_item) {
    double start = item_to_double(start_item);
    double end = item_to_double(end_item);
    double step = item_to_double(step_item);
    
    if (std::isnan(start) || std::isnan(end) || std::isnan(step)) {
        log_error("fn_range3: all arguments must be numeric");
        return ItemError;
    }
    
    if (step == 0) {
        log_error("fn_range3: step cannot be zero");
        return ItemError;
    }
    
    // Calculate number of elements
    int64_t n = 0;
    if (step > 0 && start < end) {
        n = (int64_t)ceil((end - start) / step);
    } else if (step < 0 && start > end) {
        n = (int64_t)ceil((start - end) / (-step));
    }
    
    if (n <= 0) {
        ArrayFloat* result = array_float_new(0);
        return { .array_float = result };
    }
    
    ArrayFloat* result = array_float_new(n);
    for (int64_t i = 0; i < n; i++) {
        result->items[i] = start + i * step;
    }
    
    return { .array_float = result };
}
