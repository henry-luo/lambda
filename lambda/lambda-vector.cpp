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
    if (len < 0) return ItemError;
    if (len == 0) {
        // return empty list for empty input
        List* result = list();
        return { .list = result };
    }

    TypeId vec_type = get_type_id(vec);
    TypeId scalar_type = get_type_id(scalar);
    double scalar_val = item_to_double(scalar);
    
    if (std::isnan(scalar_val)) {
        log_error("vec_scalar_op: non-numeric scalar type %d", scalar_type);
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

    // heterogeneous list: element-wise with ERROR for non-numeric
    List* result = list();
    for (int64_t i = 0; i < len; i++) {
        Item elem = vector_get(vec, i);
        TypeId elem_type = get_type_id(elem);
        
        if (!is_scalar_numeric(elem_type)) {
            // non-numeric element: produce ERROR, continue
            list_push(result, ItemError);
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
        if (scalar_type != LMD_TYPE_FLOAT && elem_type != LMD_TYPE_FLOAT && 
            op != 3 && op != 5 && res == (int64_t)res) {
            list_push(result, { .item = i2it((int64_t)res) });
        } else {
            list_push(result, push_d(res));
        }
    }
    return { .list = result };
}

//==============================================================================
// Vector-Vector Operations
//==============================================================================

static Item vec_vec_op(Item vec_a, Item vec_b, int op) {
    int64_t len_a = vector_length(vec_a);
    int64_t len_b = vector_length(vec_b);
    
    if (len_a < 0 || len_b < 0) return ItemError;
    
    // empty vectors
    if (len_a == 0 && len_b == 0) {
        List* result = list();
        return { .list = result };
    }
    if (len_a == 0 || len_b == 0) {
        List* result = list();
        return { .list = result };
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
    TypeId type_a = get_type_id(vec_a);
    TypeId type_b = get_type_id(vec_b);
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
    List* result = list();
    for (int64_t i = 0; i < len; i++) {
        Item elem_a = vector_get(vec_a, i);
        Item elem_b = vector_get(vec_b, i);
        TypeId ta = get_type_id(elem_a);
        TypeId tb = get_type_id(elem_b);
        
        if (!is_scalar_numeric(ta) || !is_scalar_numeric(tb)) {
            list_push(result, ItemError);
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
        
        if (ta != LMD_TYPE_FLOAT && tb != LMD_TYPE_FLOAT && 
            op != 3 && op != 5 && res == (int64_t)res) {
            list_push(result, { .item = i2it((int64_t)res) });
        } else {
            list_push(result, push_d(res));
        }
    }
    return { .list = result };
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
    
    log_error("fn_prod: unsupported type %d", type);
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
