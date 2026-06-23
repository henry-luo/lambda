// lambda-vector.cpp - Vectorized numeric operations for Lambda
// Implements element-wise arithmetic between scalars, arrays, lists, and ranges

#include "transpiler.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/sort.h"
#include <cmath>

static int cmp_double_asc(const void* a, const void* b, void* udata) {
    (void)udata;
    double da = *(const double*)a, db = *(const double*)b;
    return (da > db) - (da < db);
}

static int cmp_int64_asc(const void* a, const void* b, void* udata) {
    (void)udata;
    int64_t va = *(const int64_t*)a, vb = *(const int64_t*)b;
    return (va > vb) - (va < vb);
}

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
           type == LMD_TYPE_FLOAT || type == LMD_TYPE_DECIMAL ||
           type == LMD_TYPE_NUM_SIZED || type == LMD_TYPE_UINT64;
}

// check if type is a collection that supports vectorized operations
static inline bool is_vector_type(TypeId type) {
    return type == LMD_TYPE_ARRAY_NUM ||
           type == LMD_TYPE_ARRAY || type == LMD_TYPE_RANGE;
}

// check if type is any array type (homogeneous or heterogeneous)
static inline bool is_array_type(TypeId type) {
    return type == LMD_TYPE_ARRAY_NUM || type == LMD_TYPE_ARRAY;
}

// get length of a vector-like item
static int64_t vector_length(Item item) {
    TypeId type = get_type_id(item);
    switch (type) {
        case LMD_TYPE_ARRAY_NUM:   return item.array_num->length;
        case LMD_TYPE_ARRAY:        return item.array->length;
        case LMD_TYPE_RANGE:       return item.range->length;
        default:                   return -1;
    }
}

// get element from a vector-like item at index
static Item vector_get(Item item, int64_t index) {
    TypeId type = get_type_id(item);
    switch (type) {
        case LMD_TYPE_ARRAY_NUM: {
            ArrayNum* arr = item.array_num;
            switch (arr->get_elem_type()) {
                case ELEM_FLOAT:   return push_d(arr->float_items[index]);
                case ELEM_INT64:   return push_l(arr->items[index]);
                case ELEM_INT:     return { .item = i2it(arr->items[index]) };
                case ELEM_INT8:    return { .item = i8_to_item(((int8_t*)arr->data)[index]) };
                case ELEM_INT16:   return { .item = i16_to_item(((int16_t*)arr->data)[index]) };
                case ELEM_INT32:   return { .item = i32_to_item(((int32_t*)arr->data)[index]) };
                case ELEM_UINT8:   return { .item = u8_to_item(((uint8_t*)arr->data)[index]) };
                case ELEM_UINT16:  return { .item = u16_to_item(((uint16_t*)arr->data)[index]) };
                case ELEM_UINT32:  return { .item = u32_to_item(((uint32_t*)arr->data)[index]) };
                case ELEM_FLOAT16: return { .item = f16_to_item(f16_bits_to_f32(((uint16_t*)arr->data)[index])) };
                case ELEM_FLOAT32: return { .item = f32_to_item(((float*)arr->data)[index]) };
                case ELEM_UINT64: {
                    uint64_t val = ((uint64_t*)arr->data)[index];
                    uint64_t* heap_val = (uint64_t*)heap_calloc(sizeof(uint64_t), LMD_TYPE_UINT64);
                    *heap_val = val;
                    return { .item = u64_to_item(heap_val) };
                }
                case ELEM_FLOAT64: return push_d(((double*)arr->data)[index]);
                case ELEM_BOOL:    return { .item = b2it(((uint8_t*)arr->data)[index] ? BOOL_TRUE : BOOL_FALSE) };
                default:           return ItemError;
            }
        }
        case LMD_TYPE_ARRAY:
            return item.array->items[index];
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
        case LMD_TYPE_INT:       return (double)item.get_int56();
        case LMD_TYPE_INT64:     return (double)item.get_int64();
        case LMD_TYPE_FLOAT:     return item.get_double();
        case LMD_TYPE_UINT64:    return (double)item.get_uint64();
        case LMD_TYPE_NUM_SIZED: return item.get_num_sized_as_double();
        case LMD_TYPE_DECIMAL:   return item.get_double();
        default:                 return NAN;
    }
}

// check if an elem_type represents a float variant
static inline bool is_float_elem_type(ArrayNumElemType et) {
    return et == ELEM_FLOAT || et == ELEM_FLOAT16 || et == ELEM_FLOAT32 || et == ELEM_FLOAT64;
}

// read compact array element as double without boxing to Item
static inline double compact_elem_to_double(ArrayNum* arr, int64_t index) {
    switch (arr->get_elem_type()) {
        case ELEM_INT:     return (double)arr->items[index];
        case ELEM_INT64:   return (double)arr->items[index];
        case ELEM_FLOAT:   return arr->float_items[index];
        case ELEM_INT8:    return (double)((int8_t*)arr->data)[index];
        case ELEM_INT16:   return (double)((int16_t*)arr->data)[index];
        case ELEM_INT32:   return (double)((int32_t*)arr->data)[index];
        case ELEM_UINT8:   return (double)((uint8_t*)arr->data)[index];
        case ELEM_UINT16:  return (double)((uint16_t*)arr->data)[index];
        case ELEM_UINT32:  return (double)((uint32_t*)arr->data)[index];
        case ELEM_FLOAT16: return (double)f16_bits_to_f32(((uint16_t*)arr->data)[index]);
        case ELEM_FLOAT32: return (double)((float*)arr->data)[index];
        case ELEM_UINT64:  return (double)((uint64_t*)arr->data)[index];
        case ELEM_FLOAT64: return ((double*)arr->data)[index];
        default:           return NAN;
    }
}

// check if result should be float (any operand is float)
static bool needs_float_result(Item a, Item b) {
    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);
    if (ta == LMD_TYPE_FLOAT || tb == LMD_TYPE_FLOAT) return true;
    if (ta == LMD_TYPE_NUM_SIZED) {
        NumSizedType st = (NumSizedType)NUM_SIZED_SUBTYPE(a.item);
        if (st == NUM_FLOAT16 || st == NUM_FLOAT32) return true;
    }
    if (tb == LMD_TYPE_NUM_SIZED) {
        NumSizedType st = (NumSizedType)NUM_SIZED_SUBTYPE(b.item);
        if (st == NUM_FLOAT16 || st == NUM_FLOAT32) return true;
    }
    if (ta == LMD_TYPE_ARRAY_NUM && is_float_elem_type(a.array_num->get_elem_type())) return true;
    if (tb == LMD_TYPE_ARRAY_NUM && is_float_elem_type(b.array_num->get_elem_type())) return true;
    return false;
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
        return { .array = list() };
    }

    TypeId scalar_type = get_type_id(scalar);
    double scalar_val = item_to_double(scalar);

    if (std::isnan(scalar_val)) {
        log_error("vec_scalar_op: non-numeric scalar type %s", get_type_name(scalar_type));
        return ItemError;
    }

    // determine result type
    bool use_float = needs_float_result(vec, scalar) || op == 3; // div always float

    // fast path for ELEM_INT64 arrays
    if (vec_type == LMD_TYPE_ARRAY_NUM && vec.array_num->get_elem_type() == ELEM_INT64 && !use_float && op != 3 && op != 5) {
        ArrayNum* arr = vec.array_num;
        int64_t sval = (int64_t)scalar_val;
        ArrayNum* result = array_int64_new(len);
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
        return { .array_num = result };
    }

    // fast path for ELEM_INT arrays (packed int56)
    if (vec_type == LMD_TYPE_ARRAY_NUM && vec.array_num->get_elem_type() == ELEM_INT && !use_float && op != 3 && op != 5) {
        ArrayNum* arr = vec.array_num;
        int64_t sval = (int64_t)scalar_val;
        ArrayNum* result = array_num_new(ELEM_INT, len);
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
        return { .array_num = result };
    }

    // fast path for ELEM_FLOAT32 compact arrays
    if (vec_type == LMD_TYPE_ARRAY_NUM && vec.array_num->get_elem_type() == ELEM_FLOAT32) {
        ArrayNum* arr = vec.array_num;
        float sval = (float)scalar_val;
        ArrayNum* result = array_num_new(ELEM_FLOAT32, len);
        float* src = (float*)arr->data;
        float* dst = (float*)result->data;
        for (int64_t i = 0; i < len; i++) {
            float a = scalar_first ? sval : src[i];
            float b = scalar_first ? src[i] : sval;
            switch (op) {
                case 0: dst[i] = a + b; break;
                case 1: dst[i] = a - b; break;
                case 2: dst[i] = a * b; break;
                case 3: dst[i] = a / b; break;
                case 4: dst[i] = fmodf(a, b); break;
                case 5: dst[i] = powf(a, b); break;
                default: dst[i] = src[i]; break;
            }
        }
        return { .array_num = result };
    }

    if (use_float) {
        ArrayNum* result = array_float_new(len);
        for (int64_t i = 0; i < len; i++) {
            double elem_val;
            if (vec_type == LMD_TYPE_ARRAY_NUM)
                elem_val = compact_elem_to_double(vec.array_num, i);
            else
                elem_val = item_to_double(vector_get(vec, i));
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
            result->float_items[i] = res;
        }
        return { .array_num = result };
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
    if (!return_array && list_result) list_result->is_content = 1;
    return return_array ? Item{ .array = arr_result } : Item{ .array = list_result };
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
        return { .array = list() };
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
    bool use_float = needs_float_result(vec_a, vec_b) || op == 3;

    // fast path: both ELEM_INT64
    if (type_a == LMD_TYPE_ARRAY_NUM && vec_a.array_num->get_elem_type() == ELEM_INT64 &&
        type_b == LMD_TYPE_ARRAY_NUM && vec_b.array_num->get_elem_type() == ELEM_INT64 &&
        !use_float && op != 3 && op != 5) {
        ArrayNum* a = vec_a.array_num;
        ArrayNum* b = vec_b.array_num;
        ArrayNum* result = array_int64_new(len);
        for (int64_t i = 0; i < len; i++) {
            switch (op) {
                case 0: result->items[i] = a->items[i] + b->items[i]; break;
                case 1: result->items[i] = a->items[i] - b->items[i]; break;
                case 2: result->items[i] = a->items[i] * b->items[i]; break;
                case 4: result->items[i] = b->items[i] != 0 ? a->items[i] % b->items[i] : 0; break;
                default: result->items[i] = a->items[i]; break;
            }
        }
        return { .array_num = result };
    }

    // fast path: both ELEM_INT (packed int56)
    if (type_a == LMD_TYPE_ARRAY_NUM && vec_a.array_num->get_elem_type() == ELEM_INT &&
        type_b == LMD_TYPE_ARRAY_NUM && vec_b.array_num->get_elem_type() == ELEM_INT &&
        !use_float && op != 3 && op != 5) {
        ArrayNum* a = vec_a.array_num;
        ArrayNum* b = vec_b.array_num;
        ArrayNum* result = array_num_new(ELEM_INT, len);
        for (int64_t i = 0; i < len; i++) {
            switch (op) {
                case 0: result->items[i] = a->items[i] + b->items[i]; break;
                case 1: result->items[i] = a->items[i] - b->items[i]; break;
                case 2: result->items[i] = a->items[i] * b->items[i]; break;
                case 4: result->items[i] = b->items[i] != 0 ? a->items[i] % b->items[i] : 0; break;
                default: result->items[i] = a->items[i]; break;
            }
        }
        return { .array_num = result };
    }

    // fast path: both ELEM_FLOAT32
    if (type_a == LMD_TYPE_ARRAY_NUM && vec_a.array_num->get_elem_type() == ELEM_FLOAT32 &&
        type_b == LMD_TYPE_ARRAY_NUM && vec_b.array_num->get_elem_type() == ELEM_FLOAT32) {
        float* sa = (float*)vec_a.array_num->data;
        float* sb = (float*)vec_b.array_num->data;
        ArrayNum* result = array_num_new(ELEM_FLOAT32, len);
        float* dst = (float*)result->data;
        for (int64_t i = 0; i < len; i++) {
            switch (op) {
                case 0: dst[i] = sa[i] + sb[i]; break;
                case 1: dst[i] = sa[i] - sb[i]; break;
                case 2: dst[i] = sa[i] * sb[i]; break;
                case 3: dst[i] = sa[i] / sb[i]; break;
                case 4: dst[i] = fmodf(sa[i], sb[i]); break;
                case 5: dst[i] = powf(sa[i], sb[i]); break;
                default: dst[i] = sa[i]; break;
            }
        }
        return { .array_num = result };
    }

    // fast path: both homogeneous numeric arrays with float result
    if (type_a == LMD_TYPE_ARRAY_NUM && type_b == LMD_TYPE_ARRAY_NUM && use_float) {
        ArrayNum* result = array_float_new(len);
        for (int64_t i = 0; i < len; i++) {
            double a = compact_elem_to_double(vec_a.array_num, i);
            double b = compact_elem_to_double(vec_b.array_num, i);
            switch (op) {
                case 0: result->float_items[i] = a + b; break;
                case 1: result->float_items[i] = a - b; break;
                case 2: result->float_items[i] = a * b; break;
                case 3: result->float_items[i] = a / b; break;
                case 4: result->float_items[i] = fmod(a, b); break;
                case 5: result->float_items[i] = pow(a, b); break;
                default: result->float_items[i] = a; break;
            }
        }
        return { .array_num = result };
    }

    // fast path: one ARRAY_NUM + one non-ARRAY_NUM (range or list) with integer result
    if (!use_float && op != 3 && op != 5) {
        bool a_is_num = type_a == LMD_TYPE_ARRAY_NUM;
        bool b_is_num = type_b == LMD_TYPE_ARRAY_NUM;
        if (a_is_num || b_is_num) {
            ArrayNum* result = array_int64_new(len);
            for (int64_t i = 0; i < len; i++) {
                double a = a_is_num ? compact_elem_to_double(vec_a.array_num, i) : item_to_double(vector_get(vec_a, i));
                double b = b_is_num ? compact_elem_to_double(vec_b.array_num, i) : item_to_double(vector_get(vec_b, i));
                int64_t ia = (int64_t)a, ib = (int64_t)b;
                switch (op) {
                    case 0: result->items[i] = ia + ib; break;
                    case 1: result->items[i] = ia - ib; break;
                    case 2: result->items[i] = ia * ib; break;
                    case 4: result->items[i] = ib != 0 ? ia % ib : 0; break;
                    default: result->items[i] = ia; break;
                }
            }
            return { .array_num = result };
        }
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
    if (!return_array && list_result) list_result->is_content = 1;
    return return_array ? Item{ .array = arr_result } : Item{ .array = list_result };
}

//==============================================================================
// Public API: Vectorized Operations
// These are called from fn_add, fn_sub, etc. when vector types are detected
//==============================================================================

Item vec_add(Item a, Item b) {
    GUARD_ERROR2(a, b);
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
    GUARD_ERROR2(a, b);
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
    GUARD_ERROR2(a, b);
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
    GUARD_ERROR2(a, b);
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
    GUARD_ERROR2(a, b);
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
    GUARD_ERROR2(a, b);
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
Item fn_math_prod(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    log_debug("fn_math_prod: type=%d", type);

    if (type == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = item.array_num;
        if (arr->length == 0) {
            return is_float_elem_type(arr->get_elem_type()) ? push_d(1.0) : Item{ .item = i2it(1) };
        }
        if (arr->get_elem_type() == ELEM_FLOAT) {
            double prod = 1.0;
            for (int64_t i = 0; i < arr->length; i++) {
                prod *= arr->float_items[i];
            }
            return push_d(prod);
        } else if (arr->get_elem_type() == ELEM_INT || arr->get_elem_type() == ELEM_INT64) {
            int64_t prod = 1;
            for (int64_t i = 0; i < arr->length; i++) {
                prod *= arr->items[i];
            }
            return push_l(prod);
        } else if (is_float_elem_type(arr->get_elem_type())) {
            double prod = 1.0;
            for (int64_t i = 0; i < arr->length; i++) {
                prod *= compact_elem_to_double(arr, i);
            }
            return push_d(prod);
        } else {
            // compact integer types
            int64_t prod = 1;
            for (int64_t i = 0; i < arr->length; i++) {
                prod *= (int64_t)compact_elem_to_double(arr, i);
            }
            return push_l(prod);
        }
    }
    else if (type == LMD_TYPE_ARRAY) {
        List* lst = item.array;
        if (!lst || lst->length == 0) return { .item = i2it(1) };
        double prod = 1.0;
        bool has_float = false;
        for (int64_t i = 0; i < lst->length; i++) {
            Item elem = lst->items[i];
            double val = item_to_double(elem);
            if (std::isnan(val)) {
                log_error("fn_math_prod: non-numeric element at index %ld", i);
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

    log_error("fn_math_prod: unsupported type %s", get_type_name(type));
    return ItemError;
}

// cumsum(vec) - cumulative sum
Item fn_math_cumsum(Item item) {
    GUARD_ERROR1(item);
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) {
        List* result = list();
        result->is_content = 1;
        return { .array = result };
    }

    TypeId type = get_type_id(item);

    if (type == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = item.array_num;
        if (arr->get_elem_type() == ELEM_FLOAT) {
            ArrayNum* result = array_float_new(len);
            double sum = 0.0;
            for (int64_t i = 0; i < len; i++) {
                sum += arr->float_items[i];
                result->float_items[i] = sum;
            }
            return { .array_num = result };
        } else if (arr->get_elem_type() == ELEM_INT || arr->get_elem_type() == ELEM_INT64) {
            ArrayNum* result = array_int64_new(len);
            int64_t sum = 0;
            for (int64_t i = 0; i < len; i++) {
                sum += arr->items[i];
                result->items[i] = sum;
            }
            return { .array_num = result };
        } else if (is_float_elem_type(arr->get_elem_type())) {
            ArrayNum* result = array_float_new(len);
            double sum = 0.0;
            for (int64_t i = 0; i < len; i++) {
                sum += compact_elem_to_double(arr, i);
                result->float_items[i] = sum;
            }
            return { .array_num = result };
        } else {
            // compact integer types → int64 cumsum
            ArrayNum* result = array_int64_new(len);
            int64_t sum = 0;
            for (int64_t i = 0; i < len; i++) {
                sum += (int64_t)compact_elem_to_double(arr, i);
                result->items[i] = sum;
            }
            return { .array_num = result };
        }
    }
    else {
        // heterogeneous list
        ArrayNum* result = array_float_new(len);
        double sum = 0.0;
        for (int64_t i = 0; i < len; i++) {
            Item elem = vector_get(item, i);
            double val = item_to_double(elem);
            if (std::isnan(val)) {
                result->float_items[i] = NAN;
            } else {
                sum += val;
                result->float_items[i] = sum;
            }
        }
        return { .array_num = result };
    }
}

// cumprod(vec) - cumulative product
Item fn_math_cumprod(Item item) {
    GUARD_ERROR1(item);
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) {
        List* result = list();
        result->is_content = 1;
        return { .array = result };
    }

    TypeId type = get_type_id(item);

    if (type == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = item.array_num;
        if (arr->get_elem_type() == ELEM_FLOAT) {
            ArrayNum* result = array_float_new(len);
            double prod = 1.0;
            for (int64_t i = 0; i < len; i++) {
                prod *= arr->float_items[i];
                result->float_items[i] = prod;
            }
            return { .array_num = result };
        } else if (arr->get_elem_type() == ELEM_INT || arr->get_elem_type() == ELEM_INT64) {
            ArrayNum* result = array_int64_new(len);
            int64_t prod = 1;
            for (int64_t i = 0; i < len; i++) {
                prod *= arr->items[i];
                result->items[i] = prod;
            }
            return { .array_num = result };
        } else if (is_float_elem_type(arr->get_elem_type())) {
            ArrayNum* result = array_float_new(len);
            double prod = 1.0;
            for (int64_t i = 0; i < len; i++) {
                prod *= compact_elem_to_double(arr, i);
                result->float_items[i] = prod;
            }
            return { .array_num = result };
        } else {
            // compact integer types → int64 cumprod
            ArrayNum* result = array_int64_new(len);
            int64_t prod = 1;
            for (int64_t i = 0; i < len; i++) {
                prod *= (int64_t)compact_elem_to_double(arr, i);
                result->items[i] = prod;
            }
            return { .array_num = result };
        }
    }
    else {
        ArrayNum* result = array_float_new(len);
        double prod = 1.0;
        for (int64_t i = 0; i < len; i++) {
            Item elem = vector_get(item, i);
            double val = item_to_double(elem);
            if (std::isnan(val)) {
                result->float_items[i] = NAN;
            } else {
                prod *= val;
                result->float_items[i] = prod;
            }
        }
        return { .array_num = result };
    }
}

// argmin(vec) - index of minimum element
Item fn_argmin(Item item) {
    GUARD_ERROR1(item);
    int64_t len = vector_length(item);
    if (len <= 0) {
        log_error("argmin: empty collection");
        return ItemError;
    }

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
    GUARD_ERROR1(item);
    int64_t len = vector_length(item);
    if (len <= 0) {
        log_error("argmax: empty collection");
        return ItemError;
    }

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
    GUARD_ERROR2(n_item, value);
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
        Array *result = (Array *)heap_calloc(sizeof(Array), LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        result->length = 0;  result->extra = 0;
        result->items = (Item*)(result + 1);
        return { .array = result };
    }

    TypeId val_type = get_type_id(value);

    if (val_type == LMD_TYPE_INT || val_type == LMD_TYPE_INT64) {
        int64_t val = (val_type == LMD_TYPE_INT) ? value.get_int56() : value.get_int64();
        ArrayNum* result = array_int_new(n);
        for (int64_t i = 0; i < n; i++) {
            result->items[i] = val;
        }
        return { .array_num = result };
    }
    else if (val_type == LMD_TYPE_FLOAT) {
        double val = value.get_double();
        ArrayNum* result = array_float_new(n);
        for (int64_t i = 0; i < n; i++) {
            result->float_items[i] = val;
        }
        return { .array_num = result };
    }
    else {
        // spreadable array for non-numeric values (avoids list merge behavior for strings)
        Array *result = (Array *)heap_calloc(sizeof(Array) + sizeof(Item)*n, LMD_TYPE_ARRAY);
        result->type_id = LMD_TYPE_ARRAY;
        result->length = n;  result->extra = 0;
        result->items = (Item*)(result + 1);
        for (int64_t i = 0; i < n; i++) {
            result->items[i] = value;
        }
        return { .array = result };
    }
}

// dot(a, b) - dot product of two vectors
Item fn_math_dot(Item a, Item b) {
    GUARD_ERROR2(a, b);
    int64_t len_a = vector_length(a);
    int64_t len_b = vector_length(b);

    if (len_a != len_b || len_a < 0) {
        log_error("fn_math_dot: vectors must have same length");
        return ItemError;
    }

    if (len_a == 0) return push_d(0.0);

    double sum = 0.0;
    for (int64_t i = 0; i < len_a; i++) {
        double va = item_to_double(vector_get(a, i));
        double vb = item_to_double(vector_get(b, i));
        if (std::isnan(va) || std::isnan(vb)) {
            log_error("fn_math_dot: non-numeric element at index %ld", i);
            return ItemError;
        }
        sum += va * vb;
    }

    return push_d(sum);
}

// norm(vec) - Euclidean norm (L2 norm)
Item fn_math_norm(Item item) {
    GUARD_ERROR1(item);
    int64_t len = vector_length(item);
    if (len == 0) return push_d(0.0);

    double sum_sq = 0.0;
    for (int64_t i = 0; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (std::isnan(val)) {
            log_error("fn_math_norm: non-numeric element at index %ld", i);
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
Item fn_math_mean(Item item) {
    GUARD_ERROR1(item);
    // Delegate to existing fn_avg
    extern Item fn_avg(Item);
    return fn_avg(item);
}

// median(vec) - median value
Item fn_math_median(Item item) {
    GUARD_ERROR1(item);
    int64_t len = vector_length(item);
    if (len == 0) return ItemNull;

    // Copy values to sortable array
    ArrayNum* sorted = array_float_new(len);
    for (int64_t i = 0; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (std::isnan(val)) {
            log_error("fn_math_median: non-numeric element at index %ld", i);
            return ItemError;
        }
        sorted->float_items[i] = val;
    }

    insertion_sort(sorted->float_items, (size_t)len, sizeof(double), cmp_double_asc, NULL);

    if (len % 2 == 1) {
        return push_d(sorted->float_items[len / 2]);
    } else {
        return push_d((sorted->float_items[len / 2 - 1] + sorted->float_items[len / 2]) / 2.0);
    }
}

// variance(vec) - population variance
Item fn_math_variance(Item item) {
    GUARD_ERROR1(item);
    int64_t len = vector_length(item);
    if (len == 0) return ItemNull;

    // Calculate mean
    double sum = 0.0;
    for (int64_t i = 0; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (std::isnan(val)) {
            log_error("fn_math_variance: non-numeric element at index %ld", i);
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
Item fn_math_deviation(Item item) {
    GUARD_ERROR1(item);
    Item var_result = fn_math_variance(item);
    if (get_type_id(var_result) == LMD_TYPE_ERROR) return var_result;
    if (get_type_id(var_result) == LMD_TYPE_NULL) return var_result;

    double variance = var_result.get_double();
    return push_d(sqrt(variance));
}

// quantile(vec, p) - p-th quantile (0 <= p <= 1)
Item fn_math_quantile(Item item, Item p_item) {
    GUARD_ERROR2(item, p_item);
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) return ItemNull;

    double p = item_to_double(p_item);
    if (std::isnan(p) || p < 0.0 || p > 1.0) {
        log_error("fn_math_quantile: p must be between 0 and 1");
        return ItemError;
    }

    // Copy values to sortable array
    ArrayNum* sorted = array_float_new(len);
    for (int64_t i = 0; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (std::isnan(val)) {
            log_error("fn_math_quantile: non-numeric element at index %ld", i);
            return ItemError;
        }
        sorted->float_items[i] = val;
    }

    // Sort
    for (int64_t i = 0; i < len - 1; i++) {
        for (int64_t j = 0; j < len - i - 1; j++) {
            if (sorted->float_items[j] > sorted->float_items[j + 1]) {
                double tmp = sorted->float_items[j];
                sorted->float_items[j] = sorted->float_items[j + 1];
                sorted->float_items[j + 1] = tmp;
            }
        }
    }

    // Linear interpolation method (same as NumPy's default)
    double idx = p * (len - 1);
    int64_t lo = (int64_t)floor(idx);
    int64_t hi = (int64_t)ceil(idx);

    if (lo == hi || hi >= len) {
        return push_d(sorted->float_items[lo]);
    }

    double frac = idx - lo;
    return push_d(sorted->float_items[lo] * (1.0 - frac) + sorted->float_items[hi] * frac);
}

//==============================================================================
// Element-wise Math Functions
//==============================================================================

// Helper: apply unary math function element-wise
static Item vec_unary_math(Item item, double (*func)(double), const char* name) {
    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) {
        ArrayNum* result = array_float_new(0);
        return { .array_num = result };
    }

    ArrayNum* result = array_float_new(len);
    for (int64_t i = 0; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (std::isnan(val)) {
            result->float_items[i] = NAN;  // propagate error as NaN
        } else {
            result->float_items[i] = func(val);
        }
    }
    return { .array_num = result };
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
    if (type != LMD_TYPE_ARRAY &&
        type != LMD_TYPE_RANGE && type != LMD_TYPE_MAP &&
        type != LMD_TYPE_ARRAY_NUM && type != LMD_TYPE_ELEMENT &&
        type != LMD_TYPE_OBJECT) {
        return transform(collection, ItemNull);
    }

    // map/object case: iterate over key-value pairs
    if (type == LMD_TYPE_MAP || type == LMD_TYPE_OBJECT) {
        Map* mp = collection.map;
        List* result = list();

        // use item_keys to get the list of keys
        SymbolKeyList* keys = item_keys(collection);
        if (keys) {
            int64_t key_count = symbol_key_list_len(keys);
            for (int64_t i = 0; i < key_count; i++) {
                Symbol* key_sym = symbol_key_list_at(keys, i);
                Item key_item = { .item = y2it(key_sym) };
                Item value = map_get(mp, key_item);
                Item transformed = transform(value, key_item);
                list_push(result, transformed);
            }
            symbol_key_list_free(keys);
        }
        result->is_content = 1;
        return { .array = result };
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
        result->is_content = 1;
        return { .array = result };
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
    result->is_content = 1;
    return { .array = result };
}

// fn_pipe_where: filter elements where predicate is truthy
Item fn_pipe_where(Item collection, PipeMapFn predicate) {
    TypeId type = get_type_id(collection);

    // scalar case: return collection if truthy, else null
    if (type != LMD_TYPE_ARRAY &&
        type != LMD_TYPE_RANGE && type != LMD_TYPE_MAP &&
        type != LMD_TYPE_ARRAY_NUM && type != LMD_TYPE_ELEMENT &&
        type != LMD_TYPE_OBJECT) {
        Item result = predicate(collection, ItemNull);
        if (is_truthy(result)) {
            return collection;
        }
        return ItemNull;
    }

    // map/object case: filter key-value pairs (return list of values that pass predicate)
    if (type == LMD_TYPE_MAP || type == LMD_TYPE_OBJECT) {
        Map* mp = collection.map;
        List* result = list();

        SymbolKeyList* keys = item_keys(collection);
        if (keys) {
            int64_t key_count = symbol_key_list_len(keys);
            for (int64_t i = 0; i < key_count; i++) {
                Symbol* key_sym = symbol_key_list_at(keys, i);
                Item key_item = { .item = y2it(key_sym) };
                Item value = map_get(mp, key_item);
                Item pred_result = predicate(value, key_item);
                if (is_truthy(pred_result)) {
                    list_push(result, value);
                }
            }
            symbol_key_list_free(keys);
        }
        result->is_content = 1;
        return { .array = result };
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
        result->is_content = 1;
        return { .array = result };
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
    result->is_content = 1;
    return { .array = result };
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
Item fn_math_sqrt(Item item) {
    GUARD_ERROR1(item);
    // Check if scalar
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(sqrt(val));
    }
    return vec_unary_math(item, sqrt, "fn_math_sqrt");
}

// log(vec) - element-wise natural logarithm
Item fn_math_log(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(log(val));
    }
    return vec_unary_math(item, log, "fn_math_log");
}

// log10(vec) - element-wise base-10 logarithm
Item fn_math_log10(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(log10(val));
    }
    return vec_unary_math(item, log10, "fn_math_log10");
}

// exp(vec) - element-wise exponential
Item fn_math_exp(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(exp(val));
    }
    return vec_unary_math(item, exp, "fn_math_exp");
}

// sin(vec) - element-wise sine
Item fn_math_sin(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(sin(val));
    }
    return vec_unary_math(item, sin, "fn_math_sin");
}

// cos(vec) - element-wise cosine
Item fn_math_cos(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(cos(val));
    }
    return vec_unary_math(item, cos, "fn_math_cos");
}

// tan(vec) - element-wise tangent
Item fn_math_tan(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(tan(val));
    }
    return vec_unary_math(item, tan, "fn_math_tan");
}

// asin(vec) - element-wise inverse sine
Item fn_math_asin(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(asin(val));
    }
    return vec_unary_math(item, asin, "fn_math_asin");
}

// acos(vec) - element-wise inverse cosine
Item fn_math_acos(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(acos(val));
    }
    return vec_unary_math(item, acos, "fn_math_acos");
}

// atan(vec) - element-wise inverse tangent
Item fn_math_atan(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(atan(val));
    }
    return vec_unary_math(item, atan, "fn_math_atan");
}

// atan2(y, x) - two-argument inverse tangent
Item fn_math_atan2(Item item_y, Item item_x) {
    GUARD_ERROR2(item_y, item_x);
    TypeId type_y = get_type_id(item_y);
    TypeId type_x = get_type_id(item_x);
    if (is_scalar_numeric(type_y) && is_scalar_numeric(type_x)) {
        double y = item_to_double(item_y);
        double x = item_to_double(item_x);
        return push_d(atan2(y, x));
    }
    log_error("math_atan2: expected numeric arguments");
    return ItemError;
}

// sinh(vec) - element-wise hyperbolic sine
Item fn_math_sinh(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(sinh(val));
    }
    return vec_unary_math(item, sinh, "fn_math_sinh");
}

// cosh(vec) - element-wise hyperbolic cosine
Item fn_math_cosh(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(cosh(val));
    }
    return vec_unary_math(item, cosh, "fn_math_cosh");
}

// tanh(vec) - element-wise hyperbolic tangent
Item fn_math_tanh(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(tanh(val));
    }
    return vec_unary_math(item, tanh, "fn_math_tanh");
}

// asinh(vec) - element-wise inverse hyperbolic sine
Item fn_math_asinh(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(asinh(val));
    }
    return vec_unary_math(item, asinh, "fn_math_asinh");
}

// acosh(vec) - element-wise inverse hyperbolic cosine
Item fn_math_acosh(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(acosh(val));
    }
    return vec_unary_math(item, acosh, "fn_math_acosh");
}

// atanh(vec) - element-wise inverse hyperbolic tangent
Item fn_math_atanh(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(atanh(val));
    }
    return vec_unary_math(item, atanh, "fn_math_atanh");
}

// exp2(vec) - element-wise base-2 exponential
Item fn_math_exp2(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(exp2(val));
    }
    return vec_unary_math(item, exp2, "fn_math_exp2");
}

// expm1(vec) - element-wise exp(x)-1
Item fn_math_expm1(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(expm1(val));
    }
    return vec_unary_math(item, expm1, "fn_math_expm1");
}

// log2(vec) - element-wise base-2 logarithm
Item fn_math_log2(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(log2(val));
    }
    return vec_unary_math(item, log2, "fn_math_log2");
}

// pow(base, exp) - math module power function (delegates to fn_pow)
Item fn_math_pow(Item item_a, Item item_b) {
    return fn_pow(item_a, item_b);
}

// cbrt(vec) - element-wise cube root
Item fn_math_cbrt(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(cbrt(val));
    }
    return vec_unary_math(item, cbrt, "fn_math_cbrt");
}

// trunc(vec) - element-wise truncation toward zero
Item fn_trunc(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(trunc(val));
    }
    return vec_unary_math(item, trunc, "fn_trunc");
}

// item_to_bool: coerce an item to a boolean for any()/all() reductions
// true: non-zero number, true bool, non-empty string
// false: 0, false, null, empty string
static inline bool item_to_bool(Item v) {
    TypeId t = get_type_id(v);
    switch (t) {
        case LMD_TYPE_BOOL:  return v.bool_val == BOOL_TRUE;
        case LMD_TYPE_INT:   return v.get_int56() != 0;
        case LMD_TYPE_INT64: return v.get_int64() != 0;
        case LMD_TYPE_FLOAT: { double d = v.get_double(); return d != 0.0 && !std::isnan(d); }
        case LMD_TYPE_NULL:  return false;
        default:             return true;  // non-null compound values count as truthy
    }
}

// all(vec) - true iff every element is truthy; true for empty vector
Item fn_all(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_NULL) return (Item){ .item = b2it(BOOL_TRUE) };
    int64_t len = vector_length(item);
    if (len < 0) {
        log_error("fn_all: expected vector-like input, got type %d", type);
        return ItemError;
    }
    // ELEM_BOOL fast path
    if (type == LMD_TYPE_ARRAY_NUM && item.array_num->get_elem_type() == ELEM_BOOL) {
        uint8_t* data = (uint8_t*)item.array_num->data;
        for (int64_t i = 0; i < len; i++) {
            if (!data[i]) return (Item){ .item = b2it(BOOL_FALSE) };
        }
        return (Item){ .item = b2it(BOOL_TRUE) };
    }
    for (int64_t i = 0; i < len; i++) {
        if (!item_to_bool(vector_get(item, i))) return (Item){ .item = b2it(BOOL_FALSE) };
    }
    return (Item){ .item = b2it(BOOL_TRUE) };
}

// any(vec) - true iff at least one element is truthy; false for empty vector
Item fn_any(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_NULL) return (Item){ .item = b2it(BOOL_FALSE) };
    int64_t len = vector_length(item);
    if (len < 0) {
        log_error("fn_any: expected vector-like input, got type %d", type);
        return ItemError;
    }
    // ELEM_BOOL fast path
    if (type == LMD_TYPE_ARRAY_NUM && item.array_num->get_elem_type() == ELEM_BOOL) {
        uint8_t* data = (uint8_t*)item.array_num->data;
        for (int64_t i = 0; i < len; i++) {
            if (data[i]) return (Item){ .item = b2it(BOOL_TRUE) };
        }
        return (Item){ .item = b2it(BOOL_FALSE) };
    }
    for (int64_t i = 0; i < len; i++) {
        if (item_to_bool(vector_get(item, i))) return (Item){ .item = b2it(BOOL_TRUE) };
    }
    return (Item){ .item = b2it(BOOL_FALSE) };
}

// clip(x, lo, hi) - clamp element-wise to [lo, hi]
// scalar x: returns scalar; vector x: returns ArrayNum (float result)
// lo/hi must be scalar numerics
Item fn_clip(Item item, Item lo_item, Item hi_item) {
    GUARD_ERROR3(item, lo_item, hi_item);
    TypeId lo_type = get_type_id(lo_item);
    TypeId hi_type = get_type_id(hi_item);
    if (!is_scalar_numeric(lo_type) || !is_scalar_numeric(hi_type)) {
        log_error("fn_clip: lo and hi must be scalar numerics");
        return ItemError;
    }
    double lo = item_to_double(lo_item);
    double hi = item_to_double(hi_item);
    if (lo > hi) {
        log_error("fn_clip: lo (%g) must be <= hi (%g)", lo, hi);
        return ItemError;
    }
    TypeId type = get_type_id(item);
    if (is_scalar_numeric(type)) {
        double val = item_to_double(item);
        if (std::isnan(val)) return push_d(NAN);
        if (val < lo) val = lo;
        else if (val > hi) val = hi;
        return push_d(val);
    }
    int64_t len = vector_length(item);
    if (len < 0) {
        log_error("fn_clip: expected numeric or vector input, got type %d", type);
        return ItemError;
    }
    if (len == 0) {
        ArrayNum* result = array_float_new(0);
        return { .array_num = result };
    }
    ArrayNum* result = array_float_new(len);
    for (int64_t i = 0; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (std::isnan(val)) {
            result->float_items[i] = NAN;
        } else {
            if (val < lo) val = lo;
            else if (val > hi) val = hi;
            result->float_items[i] = val;
        }
    }
    return { .array_num = result };
}

// hypot(y, x) - Euclidean distance sqrt(y*y + x*x)
Item fn_math_hypot(Item item_y, Item item_x) {
    GUARD_ERROR2(item_y, item_x);
    TypeId type_y = get_type_id(item_y);
    TypeId type_x = get_type_id(item_x);
    if (is_scalar_numeric(type_y) && is_scalar_numeric(type_x)) {
        double y = item_to_double(item_y);
        double x = item_to_double(item_x);
        return push_d(hypot(y, x));
    }
    log_error("math_hypot: expected numeric arguments");
    return ItemError;
}

// log1p(vec) - element-wise ln(1+x), precise for small x
Item fn_math_log1p(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        return push_d(log1p(val));
    }
    return vec_unary_math(item, log1p, "fn_math_log1p");
}

// sign(vec) - element-wise sign (-1, 0, 1)
Item fn_sign(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        double val = item_to_double(item);
        int64_t s = (val > 0) ? 1 : (val < 0) ? -1 : 0;
        return { .item = i2it(s) };
    }

    int64_t len = vector_length(item);
    if (len < 0) return ItemError;
    if (len == 0) {
        ArrayNum* result = array_int64_new(0);
        return { .array_num = result };
    }

    ArrayNum* result = array_int64_new(len);
    for (int64_t i = 0; i < len; i++) {
        double val = item_to_double(vector_get(item, i));
        if (std::isnan(val)) {
            result->items[i] = 0;  // treat NaN as 0
        } else {
            result->items[i] = (val > 0) ? 1 : (val < 0) ? -1 : 0;
        }
    }
    return { .array_num = result };
}

// math.random(seed) - pure functional PRNG using SplitMix64
// Returns a list [float_value, new_seed] where float_value is in [0.0, 1.0)
// Usage: let x, newSeed = math.random(42)
Item fn_math_random(Item seed_item) {
    GUARD_ERROR1(seed_item);
    uint64_t state = (uint64_t)it2i(seed_item);
    // SplitMix64 algorithm
    state += 0x9e3779b97f4a7c15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    z = z ^ (z >> 31);
    // convert to double in [0.0, 1.0)
    double value = (double)(z >> 11) * 0x1.0p-53;
    List* result = list();
    list_push(result, push_d(value));
    list_push(result, push_l((int64_t)state));
    return { .array = result };
}

//==============================================================================
// Vector Manipulation Functions
//==============================================================================

// reverse(vec) - reverse order of elements
// string/symbol passthrough: strings are singular, not iterable
Item fn_reverse(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL) return item;

    int64_t len = vector_length(item);
    if (len == 0) {
        List* result = list();
        result->is_content = 1;
        return { .array = result };
    }

    if (type == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = item.array_num;
        if (arr->get_elem_type() == ELEM_INT) {
            // ELEM_INT (from literals): return content List for spreading
            List* result = list();
            for (int64_t i = len - 1; i >= 0; i--)
                list_push(result, vector_get(item, i));
            result->is_content = 1;
            return { .array = result };
        } else if (arr->get_elem_type() == ELEM_FLOAT) {
            ArrayNum* result = array_float_new(len);
            for (int64_t i = 0; i < len; i++)
                result->float_items[i] = arr->float_items[len - 1 - i];
            return { .array_num = result };
        } else {
            ArrayNum* result = array_int64_new(len);
            for (int64_t i = 0; i < len; i++)
                result->items[i] = arr->items[len - 1 - i];
            return { .array_num = result };
        }
    }
    else {
        List* result = list();
        for (int64_t i = len - 1; i >= 0; i--) {
            list_push(result, vector_get(item, i));
        }
        result->is_content = 1;
        return { .array = result };
    }
}

// sort(vec) - sort in ascending order
// string/symbol passthrough: strings are singular, not iterable
Item fn_sort1(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL) return item;

    int64_t len = vector_length(item);
    if (len == 0) {
        List* result = list();
        result->is_content = 1;
        return { .array = result };
    }

    if (type == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = item.array_num;
        if (arr->get_elem_type() == ELEM_FLOAT) {
            ArrayNum* result = array_float_new(len);
            for (int64_t i = 0; i < len; i++) result->float_items[i] = arr->float_items[i];
            insertion_sort(result->float_items, (size_t)len, sizeof(double), cmp_double_asc, NULL);
            return { .array_num = result };
        } else {
            ArrayNum* result = array_int64_new(len);
            for (int64_t i = 0; i < len; i++) result->items[i] = arr->items[i];
            insertion_sort(result->items, (size_t)len, sizeof(int64_t), cmp_int64_asc, NULL);
            return { .array_num = result };
        }
    }
    else {
        // For mixed types, convert to float and sort
        ArrayNum* result = array_float_new(len);
        for (int64_t i = 0; i < len; i++) {
            result->float_items[i] = item_to_double(vector_get(item, i));
        }
        insertion_sort(result->float_items, (size_t)len, sizeof(double), cmp_double_asc, NULL);
        return { .array_num = result };
    }
}

// sort_by_keys(values, keys, descending) - sort values array in-place by corresponding keys
// Keys are compared as doubles; values preserve their original Item type
void fn_sort_by_keys(Item values, Item keys, int64_t descending) {
    int64_t len = vector_length(values);
    int64_t key_len = vector_length(keys);
    if (len == 0 || key_len == 0) {
        return;
    }
    if (len != key_len) {
        log_error("sort_by_keys: values length %ld != keys length %ld", len, key_len);
        return;
    }

    // build index permutation array and extract key doubles
    int64_t* indices = (int64_t*)mem_calloc(len, sizeof(int64_t), MEM_CAT_EVAL);
    double*  key_vals = (double*)mem_calloc(len, sizeof(double), MEM_CAT_EVAL);
    for (int64_t i = 0; i < len; i++) {
        indices[i] = i;
        key_vals[i] = item_to_double(vector_get(keys, i));
    }

    // bubble sort indices by key_vals
    for (int64_t i = 0; i < len - 1; i++) {
        for (int64_t j = 0; j < len - i - 1; j++) {
            bool swap = descending ? (key_vals[indices[j]] < key_vals[indices[j + 1]])
                                   : (key_vals[indices[j]] > key_vals[indices[j + 1]]);
            if (swap) {
                int64_t tmp = indices[j];
                indices[j] = indices[j + 1];
                indices[j + 1] = tmp;
            }
        }
    }

    // rearrange values array in-place using the sorted index permutation
    // values must be an Array (LMD_TYPE_ARRAY) with items pointer
    Array* arr = values.array;
    Item* temp = (Item*)mem_alloc(len * sizeof(Item), MEM_CAT_EVAL);
    for (int64_t i = 0; i < len; i++) {
        temp[i] = arr->items[indices[i]];
    }
    memcpy(arr->items, temp, len * sizeof(Item));
    mem_free(temp);
    mem_free(indices);
    mem_free(key_vals);

    // mark as non-spreadable so the sorted result displays as a single array
    // (without this, list_push_spread would spread individual items)
    arr->is_spreadable = false;
}

// sort(vec, option) - sort with direction, key function, or options map
// 2nd arg dispatch:
//   symbol/string → direction ('asc or 'desc)
//   function      → key extractor fn (ascending)
//   map           → {dir: 'asc|'desc, by: key_fn}
// string/symbol passthrough: strings are singular, not iterable
Item fn_sort2(Item item, Item dir_item) {
    GUARD_ERROR2(item, dir_item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL) return item;

    int64_t len = vector_length(item);
    if (len == 0) {
        List* result = list();
        result->is_content = 1;
        return { .array = result };
    }

    // Parse the 2nd argument: direction, key function, or options map
    bool descending = false;
    Function* key_fn = NULL;
    TypeId dir_type = get_type_id(dir_item);

    if (dir_type == LMD_TYPE_STRING) {
        String* str = dir_item.get_string();
        if (str && (strcmp(str->chars, "desc") == 0 || strcmp(str->chars, "descending") == 0)) {
            descending = true;
        }
    } else if (dir_type == LMD_TYPE_SYMBOL) {
        Symbol* sym = dir_item.get_symbol();
        if (sym && (strcmp(sym->chars, "desc") == 0 || strcmp(sym->chars, "descending") == 0)) {
            descending = true;
        }
    } else if (dir_type == LMD_TYPE_FUNC) {
        // 2nd arg is a key extractor function
        key_fn = dir_item.function;
    } else if (dir_type == LMD_TYPE_MAP || dir_type == LMD_TYPE_OBJECT) {
        // 2nd arg is an options map: {dir: 'asc|'desc, by: key_fn}
        Map* options_map = dir_item.map;

        // extract 'dir' field
        Item dir_val = map_get(options_map, (Item){.item = s2it(heap_create_name("dir", 3))});
        TypeId dir_val_type = get_type_id(dir_val);
        if (dir_val_type == LMD_TYPE_STRING) {
            String* str = dir_val.get_string();
            if (str && (strcmp(str->chars, "desc") == 0 || strcmp(str->chars, "descending") == 0)) {
                descending = true;
            }
        } else if (dir_val_type == LMD_TYPE_SYMBOL) {
            Symbol* sym = dir_val.get_symbol();
            if (sym && (strcmp(sym->chars, "desc") == 0 || strcmp(sym->chars, "descending") == 0)) {
                descending = true;
            }
        }

        // extract 'by' field (key function)
        Item by_val = map_get(options_map, (Item){.item = s2it(heap_create_name("by", 2))});
        if (get_type_id(by_val) == LMD_TYPE_FUNC) {
            key_fn = by_val.function;
        }
    }

    // If we have a key function, sort by extracted keys (works for any collection type)
    if (key_fn) {
        // build an Array of Items to sort
        Array* result = array();
        result->capacity = len;
        result->length = len;
        result->items = (Item*)heap_data_calloc(len * sizeof(Item));
        for (int64_t i = 0; i < len; i++) {
            result->items[i] = vector_get(item, i);
        }

        // extract keys using the key function
        Item* key_vals = (Item*)mem_alloc(len * sizeof(Item), MEM_CAT_EVAL);
        int64_t* indices = (int64_t*)mem_alloc(len * sizeof(int64_t), MEM_CAT_EVAL);
        for (int64_t i = 0; i < len; i++) {
            indices[i] = i;
            Item key_result = fn_call1(key_fn, result->items[i]);
            if (get_type_id(key_result) == LMD_TYPE_ERROR) {
                mem_free(key_vals);
                mem_free(indices);
                return key_result;  // propagate error
            }
            key_vals[i] = key_result;
        }

        // sort indices by key values using generic comparison (fn_lt/fn_gt)
        for (int64_t i = 0; i < len - 1; i++) {
            for (int64_t j = 0; j < len - i - 1; j++) {
                Bool cmp = descending ? fn_lt(key_vals[indices[j]], key_vals[indices[j + 1]])
                                      : fn_gt(key_vals[indices[j]], key_vals[indices[j + 1]]);
                if (cmp == BOOL_TRUE) {
                    int64_t tmp = indices[j];
                    indices[j] = indices[j + 1];
                    indices[j + 1] = tmp;
                }
            }
        }

        // rearrange items by sorted indices
        Item* temp = (Item*)mem_alloc(len * sizeof(Item), MEM_CAT_EVAL);
        for (int64_t i = 0; i < len; i++) {
            temp[i] = result->items[indices[i]];
        }
        memcpy(result->items, temp, len * sizeof(Item));
        mem_free(temp);
        mem_free(indices);
        mem_free(key_vals);

        result->is_spreadable = false;
        return { .array = result };
    }

    // No key function - sort by value with direction (original behavior)
    if (type == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = item.array_num;
        if (arr->get_elem_type() == ELEM_FLOAT) {
            ArrayNum* result = array_float_new(len);
            for (int64_t i = 0; i < len; i++) result->float_items[i] = arr->float_items[i];
            for (int64_t i = 0; i < len - 1; i++) {
                for (int64_t j = 0; j < len - i - 1; j++) {
                    bool swap = descending ? (result->float_items[j] < result->float_items[j + 1])
                                           : (result->float_items[j] > result->float_items[j + 1]);
                    if (swap) {
                        double tmp = result->float_items[j];
                        result->float_items[j] = result->float_items[j + 1];
                        result->float_items[j + 1] = tmp;
                    }
                }
            }
            return { .array_num = result };
        } else {
            ArrayNum* result = array_int64_new(len);
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
            return { .array_num = result };
        }
    }
    else {
        ArrayNum* result = array_float_new(len);
        for (int64_t i = 0; i < len; i++) {
            result->float_items[i] = item_to_double(vector_get(item, i));
        }
        for (int64_t i = 0; i < len - 1; i++) {
            for (int64_t j = 0; j < len - i - 1; j++) {
                bool swap = descending ? (result->float_items[j] < result->float_items[j + 1])
                                       : (result->float_items[j] > result->float_items[j + 1]);
                if (swap) {
                    double tmp = result->float_items[j];
                    result->float_items[j] = result->float_items[j + 1];
                    result->float_items[j + 1] = tmp;
                }
            }
        }
        return { .array_num = result };
    }
}

// reduce(collection, fn) - fold/accumulate a collection using a binary function
// fn receives (accumulator, current_element) and returns new accumulator
// Initial accumulator is the first element; fn is called starting from the second element
// Returns ItemNull for empty collections
Item fn_reduce(Item collection, Item func_item) {
    GUARD_ERROR2(collection, func_item);

    TypeId func_type = get_type_id(func_item);
    if (func_type != LMD_TYPE_FUNC) {
        log_error("reduce: 2nd argument must be a function, got type: %s", get_type_name(func_type));
        return ItemError;
    }
    Function* fn = func_item.function;

    int64_t len = vector_length(collection);
    if (len <= 0) return ItemNull;  // empty collection
    if (len == 1) return vector_get(collection, 0);  // single element

    Item acc = vector_get(collection, 0);
    for (int64_t i = 1; i < len; i++) {
        Item elem = vector_get(collection, i);
        acc = fn_call2(fn, acc, elem);
        if (get_type_id(acc) == LMD_TYPE_ERROR) return acc;  // propagate error
    }
    return acc;
}

// unique(vec) - remove duplicates
// list input → spreadable array; array input → preserve is_spreadable flag
// string/symbol passthrough: strings are singular, not iterable
Item fn_unique(Item item) {
    GUARD_ERROR1(item);
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL) return item;

    int64_t len = vector_length(item);

    // determine spreadable flag: lists → true, arrays → follow input
    bool spreadable = true; // default for list input
    if (type == LMD_TYPE_ARRAY) {
        spreadable = item.array->is_spreadable;
    }
    // ARRAY_NUM: always spreadable for display (like old ARRAY_INT fallback)

    if (len == 0) {
        Array* result = array();
        result->is_spreadable = spreadable;
        return { .array = result };
    }

    // fast path for homogeneous numeric arrays
    if (type == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = item.array_num;
        Array* result = array();
        if (arr->get_elem_type() == ELEM_FLOAT) {
            for (int64_t i = 0; i < len; i++) {
                double val = arr->float_items[i];
                bool found = false;
                for (int64_t j = 0; j < (int64_t)result->length; j++) {
                    double res_val = result->items[j].get_double();
                    if (val == res_val || (std::isnan(val) && std::isnan(res_val))) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    array_push(result, push_d(val));
                }
            }
        } else {
            for (int64_t i = 0; i < len; i++) {
                int64_t val = arr->items[i];
                bool found = false;
                for (int64_t j = 0; j < (int64_t)result->length; j++) {
                    if (result->items[j].get_int64() == val) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    array_push(result, push_l(val));
                }
            }
        }
        result->is_spreadable = spreadable;
        return { .array = result };
    }

    // generic path: use fn_eq for type-aware comparison (handles strings, symbols, etc.)
    Array* result = array();
    for (int64_t i = 0; i < len; i++) {
        Item elem = vector_get(item, i);

        bool found = false;
        for (int64_t j = 0; j < (int64_t)result->length; j++) {
            if (fn_eq(elem, result->items[j]) == BOOL_TRUE) {
                found = true;
                break;
            }
        }
        if (!found) {
            array_push(result, elem);
        }
    }
    result->is_spreadable = spreadable;
    return { .array = result };
}

// take(vec, n) - first n elements
// string/symbol: delegates to fn_substring(str, 0, n)
Item fn_take(Item vec, Item n_item) {
    GUARD_ERROR2(vec, n_item);

    TypeId type = get_type_id(vec);
    // string/symbol: take = substring(str, 0, n)
    if (type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL) {
        Item zero = {.item = i2it(0)};
        return fn_substring(vec, zero, n_item);
    }

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

    if (type == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = vec.array_num;
        if (arr->get_elem_type() == ELEM_INT) {
            // ELEM_INT (from literals): return content List for spreading
            List* result = list();
            for (int64_t i = 0; i < n; i++) list_push(result, vector_get(vec, i));
            result->is_content = 1;
            return { .array = result };
        } else if (arr->get_elem_type() == ELEM_FLOAT) {
            ArrayNum* result = array_float_new(n);
            for (int64_t i = 0; i < n; i++) result->float_items[i] = arr->float_items[i];
            return { .array_num = result };
        } else {
            ArrayNum* result = array_int64_new(n);
            for (int64_t i = 0; i < n; i++) result->items[i] = arr->items[i];
            return { .array_num = result };
        }
    }
    else {
        List* result = list();
        for (int64_t i = 0; i < n; i++) list_push(result, vector_get(vec, i));
        result->is_content = 1;
        return { .array = result };
    }
}

// drop(vec, n) - drop first n elements
// string/symbol: delegates to fn_substring(str, n, len)
Item fn_drop(Item vec, Item n_item) {
    GUARD_ERROR2(vec, n_item);

    TypeId type = get_type_id(vec);
    // string/symbol: drop = substring(str, n, char_count)
    if (type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL) {
        int64_t char_count = fn_len(vec);
        Item end = {.item = i2it(char_count)};
        return fn_substring(vec, n_item, end);
    }

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

    if (type == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = vec.array_num;
        if (arr->get_elem_type() == ELEM_INT) {
            // ELEM_INT (from literals): return content List for spreading
            List* result = list();
            for (int64_t i = n; i < len; i++) list_push(result, vector_get(vec, i));
            result->is_content = 1;
            return { .array = result };
        } else if (arr->get_elem_type() == ELEM_FLOAT) {
            ArrayNum* result = array_float_new(new_len);
            for (int64_t i = 0; i < new_len; i++) result->float_items[i] = arr->float_items[n + i];
            return { .array_num = result };
        } else {
            ArrayNum* result = array_int64_new(new_len);
            for (int64_t i = 0; i < new_len; i++) result->items[i] = arr->items[n + i];
            return { .array_num = result };
        }
    }
    else {
        List* result = list();
        for (int64_t i = n; i < len; i++) list_push(result, vector_get(vec, i));
        result->is_content = 1;
        return { .array = result };
    }
}

// slice(vec, start, end) - extract elements from start (inclusive) to end (exclusive)
// Works for arrays, lists, and strings
Item fn_slice(Item vec, Item start_item, Item end_item) {
    GUARD_ERROR3(vec, start_item, end_item);
    TypeId type = get_type_id(vec);

    // null slices to null
    if (type == LMD_TYPE_NULL) return ItemNull;

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

    if (type == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* arr = vec.array_num;
        if (arr->get_elem_type() == ELEM_INT) {
            // ELEM_INT (from literals): return content List for spreading
            List* result = list();
            for (int64_t i = start; i < end; i++) list_push(result, vector_get(vec, i));
            result->is_content = 1;
            return { .array = result };
        } else if (arr->get_elem_type() == ELEM_FLOAT) {
            ArrayNum* result = array_float_new(new_len);
            for (int64_t i = 0; i < new_len; i++) result->float_items[i] = arr->float_items[start + i];
            return { .array_num = result };
        } else {
            ArrayNum* result = array_int64_new(new_len);
            for (int64_t i = 0; i < new_len; i++) result->items[i] = arr->items[start + i];
            return { .array_num = result };
        }
    }
    else {
        List* result = list();
        for (int64_t i = start; i < end; i++) list_push(result, vector_get(vec, i));
        result->is_content = 1;
        return { .array = result };
    }
}

// ============================================================================
// view(arr, start, end) — read-only view sharing arr's storage.
// Allocates a new ArrayNum whose data pointer is base->data + start*elem_size.
// The view's shape side-table holds a reference to the base, keeping it alive
// across GC; the base is also pinned so its data buffer cannot be relocated.
// Mutation through the view raises an error (see fn_array_set / array_num_set_item).
// ============================================================================
Item fn_subview(Item vec, Item start_item, Item end_item) {
    GUARD_ERROR3(vec, start_item, end_item);
    TypeId type = get_type_id(vec);
    if (type != LMD_TYPE_ARRAY_NUM) {
        log_error("fn_view: only typed arrays (ArrayNum) can be viewed; got type %d", type);
        return ItemError;
    }
    ArrayNum* base = vec.array_num;
    if (!base) return ItemError;
    if (!base->is_heap) {
        log_error("fn_view: cannot view arena-backed array; copy() first to get a heap array");
        return ItemError;
    }
    TypeId st = get_type_id(start_item);
    TypeId et = get_type_id(end_item);
    if ((st != LMD_TYPE_INT && st != LMD_TYPE_INT64) ||
        (et != LMD_TYPE_INT && et != LMD_TYPE_INT64)) {
        log_error("fn_view: start and end must be integers");
        return ItemError;
    }
    int64_t start = (st == LMD_TYPE_INT) ? start_item.get_int56() : start_item.get_int64();
    int64_t end   = (et == LMD_TYPE_INT) ? end_item.get_int56()   : end_item.get_int64();
    int64_t len = base->length;
    if (start < 0) start = len + start;
    if (end < 0) end = len + end;
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start > end) start = end;
    int64_t view_len = end - start;

    ArrayNumElemType etype = base->get_elem_type();
    int elem_size = ELEM_TYPE_SIZE[etype >> 4];

    ArrayNum* view = (ArrayNum*)heap_calloc(sizeof(ArrayNum), LMD_TYPE_ARRAY_NUM);
    if (!view) return ItemError;
    view->type_id = LMD_TYPE_ARRAY_NUM;
    view->set_elem_type(etype);
    view->is_ndim = 1;
    view->is_view = 1;
    // pre-adjusted data pointer — element 0 of view is element `start` of base
    view->data = (void*)((char*)base->data + start * (size_t)elem_size);
    view->length = view_len;
    view->capacity = view_len;

    // allocate shape side-table: ndim=1, base ref, offset for diagnostics, shape[0]=len, strides[0]=1
    size_t shape_bytes = sizeof(ArrayNumShape) + 2 * sizeof(int64_t);
    ArrayNumShape* shape = (ArrayNumShape*)heap_data_calloc(shape_bytes);
    if (!shape) return ItemError;
    shape->ndim = 1;
    shape->is_c_contig = 1;
    shape->is_f_contig = 1;
    shape->offset = start;
    shape->base = (void*)base;
    shape->data[0] = view_len;  // shape[0]
    shape->data[1] = 1;          // strides[0] in elements
    view->extra = (int64_t)(uintptr_t)shape;

    // pin the base so its data buffer cannot be relocated by GC compaction
    base->is_pinned = 1;
    return { .array_num = view };
}

// ============================================================================
// reshape(arr, shape_list) — view with new dimensionality.
// Source must be C-contiguous (any owned 1-D array, or another C-contiguous view).
// shape_list is an array of int dimensions whose product must equal arr->length.
// Returns a view sharing arr's storage, with new shape and row-major strides.
// ============================================================================
Item fn_reshape(Item vec, Item shape_item) {
    GUARD_ERROR2(vec, shape_item);
    TypeId vt = get_type_id(vec);
    if (vt != LMD_TYPE_ARRAY_NUM) {
        log_error("fn_reshape: source must be a typed numeric array, got type %d", vt);
        return ItemError;
    }
    ArrayNum* base = vec.array_num;
    if (!base) return ItemError;
    if (!base->is_heap) {
        log_error("fn_reshape: cannot reshape arena-backed array; copy() first");
        return ItemError;
    }
    // contiguous check: owned 1-D array is contiguous; existing view must have is_c_contig set
    if (base->is_view || base->is_ndim) {
        ArrayNumShape* bshape = (ArrayNumShape*)(uintptr_t)base->extra;
        if (!bshape || !bshape->is_c_contig) {
            log_error("fn_reshape: source must be C-contiguous (use copy() to materialize first)");
            return ItemError;
        }
    }

    // collect new shape from shape_item
    int64_t st_len = vector_length(shape_item);
    if (st_len < 0) {
        log_error("fn_reshape: shape argument must be a vector of integers");
        return ItemError;
    }
    if (st_len < 1 || st_len > 32) {
        log_error("fn_reshape: ndim must be in [1, 32], got %lld", (long long)st_len);
        return ItemError;
    }

    // read dims, validate, compute product
    int64_t total = 1;
    int64_t dims_stack[32];
    for (int64_t i = 0; i < st_len; i++) {
        Item d = vector_get(shape_item, i);
        TypeId dt = get_type_id(d);
        int64_t dim;
        if (dt == LMD_TYPE_INT) dim = d.get_int56();
        else if (dt == LMD_TYPE_INT64) dim = d.get_int64();
        else {
            log_error("fn_reshape: shape entries must be integers");
            return ItemError;
        }
        if (dim < 0) {
            log_error("fn_reshape: shape entries must be non-negative, got %lld at axis %lld",
                      (long long)dim, (long long)i);
            return ItemError;
        }
        dims_stack[i] = dim;
        total *= dim;
    }
    if (total != base->length) {
        log_error("fn_reshape: shape product (%lld) does not match array length (%lld)",
                  (long long)total, (long long)base->length);
        return ItemError;
    }

    ArrayNumElemType etype = base->get_elem_type();

    // allocate the view header
    ArrayNum* view = (ArrayNum*)heap_calloc(sizeof(ArrayNum), LMD_TYPE_ARRAY_NUM);
    if (!view) return ItemError;
    view->type_id = LMD_TYPE_ARRAY_NUM;
    view->set_elem_type(etype);
    view->is_ndim = 1;
    view->is_view = 1;
    view->data = base->data;            // alias entire data
    view->length = base->length;
    view->capacity = base->length;

    // allocate shape side-table: header + shape[ndim] + strides[ndim]
    size_t shape_bytes = sizeof(ArrayNumShape) + 2 * (size_t)st_len * sizeof(int64_t);
    ArrayNumShape* shape = (ArrayNumShape*)heap_data_calloc(shape_bytes);
    if (!shape) return ItemError;
    shape->ndim = (uint8_t)st_len;
    shape->is_c_contig = 1;
    shape->is_f_contig = (st_len == 1) ? 1 : 0;
    shape->offset = 0;
    shape->base = (void*)base;

    // copy shape and compute C-contiguous strides
    int64_t* shp = array_num_shape_dims(shape);
    int64_t* str = array_num_shape_strides(shape);
    for (int64_t i = 0; i < st_len; i++) shp[i] = dims_stack[i];
    int64_t stride = 1;
    for (int64_t i = st_len - 1; i >= 0; i--) {
        str[i] = stride;
        stride *= dims_stack[i];
    }
    view->extra = (int64_t)(uintptr_t)shape;

    base->is_pinned = 1;
    return { .array_num = view };
}

// shape(arr) - returns a list of dimensions; for 1-D returns [length]
Item fn_shape(Item vec) {
    GUARD_ERROR1(vec);
    TypeId vt = get_type_id(vec);
    if (vt != LMD_TYPE_ARRAY_NUM) {
        log_error("fn_shape: expected typed numeric array, got type %d", vt);
        return ItemError;
    }
    ArrayNum* arr = vec.array_num;
    if (!arr) return ItemError;
    List* result = list();
    if (arr->is_ndim && arr->extra) {
        ArrayNumShape* shape = (ArrayNumShape*)(uintptr_t)arr->extra;
        int64_t* shp = array_num_shape_dims(shape);
        for (int64_t i = 0; i < shape->ndim; i++) list_push(result, push_l(shp[i]));
    } else {
        list_push(result, push_l(arr->length));
    }
    return { .array = result };
}

// ndim(arr) - returns number of dimensions
Item fn_ndim(Item vec) {
    GUARD_ERROR1(vec);
    TypeId vt = get_type_id(vec);
    if (vt != LMD_TYPE_ARRAY_NUM) return push_l(0);
    ArrayNum* arr = vec.array_num;
    if (!arr) return push_l(0);
    if (arr->is_ndim && arr->extra) {
        ArrayNumShape* shape = (ArrayNumShape*)(uintptr_t)arr->extra;
        return push_l((int64_t)shape->ndim);
    }
    return push_l(1);
}

// is_view(arr) - returns true if arr is a view over another array
Item fn_is_view(Item vec) {
    GUARD_ERROR1(vec);
    TypeId type = get_type_id(vec);
    if (type != LMD_TYPE_ARRAY_NUM) {
        return { .item = b2it(BOOL_FALSE) };
    }
    return { .item = b2it(vec.array_num->is_view ? BOOL_TRUE : BOOL_FALSE) };
}

// zip(v1, v2) - pair elements into tuples
Item fn_zip(Item a, Item b) {
    GUARD_ERROR2(a, b);
    int64_t len_a = vector_length(a);
    int64_t len_b = vector_length(b);

    if (len_a < 0 || len_b < 0) return ItemError;

    int64_t len = (len_a < len_b) ? len_a : len_b;  // take shorter length

    List* result = list();
    for (int64_t i = 0; i < len; i++) {
        List* pair = list();
        list_push(pair, vector_get(a, i));
        list_push(pair, vector_get(b, i));
        list_push(result, { .array = pair });
    }
    result->is_content = 1;
    return { .array = result };
}

// range(start, end, step) - generate range with step
Item fn_range3(Item start_item, Item end_item, Item step_item) {
    GUARD_ERROR3(start_item, end_item, step_item);
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
        ArrayNum* result = array_float_new(0);
        return { .array_num = result };
    }

    ArrayNum* result = array_float_new(n);
    for (int64_t i = 0; i < n; i++) {
        result->float_items[i] = start + i * step;
    }

    return { .array_num = result };
}
