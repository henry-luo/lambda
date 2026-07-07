// lambda-vector.cpp - Vectorized numeric operations for Lambda
// Implements element-wise arithmetic between scalars, arrays, lists, and ranges

#include "transpiler.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/sort.h"
#include "../lib/image.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <math.h>
#include <type_traits>

// SIMD vectorization hint for the contiguous numeric kernels below.  Uses each
// compiler's native loop-vectorization pragma so there is no OpenMP runtime or
// flag dependency (fits the C+ convention); the actual vector ISA comes from
// the release build's -O3 -march=native.  At -O0 (debug) it is a harmless hint.
#if defined(__clang__)
  #define LMD_VEC_LOOP _Pragma("clang loop vectorize(enable) interleave(enable)")
#elif defined(__GNUC__)
  #define LMD_VEC_LOOP _Pragma("GCC ivdep")
#else
  #define LMD_VEC_LOOP
#endif

static int cmp_double_asc(const void* a, const void* b, void* udata) {
    (void)udata;
    double da = *(const double*)a, db = *(const double*)b;
    return (da > db) - (da < db);
}

static bool item_to_integral_index(Item item, int64_t* out) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT) {
        *out = item.get_int56();
        return true;
    }
    if (type == LMD_TYPE_INT64) {
        *out = item.get_int64();
        return true;
    }
    if (type == LMD_TYPE_FLOAT) {
        double val = item.get_double();
        if (isnan(val) || isinf(val)) return false;
        if (val < (double)LLONG_MIN || val > (double)LLONG_MAX) return false;
        int64_t int_val = (int64_t)val;
        if ((double)int_val != val) return false;
        *out = int_val;
        return true;
    }
    return false;
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
                case ELEM_UINT8_CLAMPED: return { .item = u8_to_item(((uint8_t*)arr->data)[index]) };
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

static Array* vector_to_plain_array(Item item, int64_t len) {
    Array* result = array();
    result->capacity = len;
    result->length = len;
    result->items = (Item*)heap_data_calloc(len * sizeof(Item));
    for (int64_t i = 0; i < len; i++) {
        result->items[i] = vector_get(item, i);
    }
    result->is_spreadable = false;
    return result;
}

static void stable_sort_items_by_total_order(Item* items, int64_t len, bool descending) {
    for (int64_t i = 1; i < len; i++) {
        Item value = items[i];
        int64_t j = i - 1;
        while (j >= 0) {
            int cmp = total_cmp(items[j], value);
            bool move = descending ? (cmp < 0) : (cmp > 0);
            if (!move) break;
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = value;
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
        case ELEM_UINT8_CLAMPED: return (double)((uint8_t*)arr->data)[index];
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
// Contiguous elementwise kernels (auto-vectorization targets)
//
// Each kernel is a single straight-line, __restrict-qualified loop with the op
// selected at compile time via `if constexpr`, so the op-switch lives OUTSIDE
// the loop body and the compiler can auto-vectorize.  Only the vectorizable ops
// live here (add/sub/mul, and div for floating types); mod/pow/integer-div stay
// on scalar loops at the call sites.  Op encoding: 0=add 1=sub 2=mul 3=div.
//==============================================================================

// d[i] = a[i] OP b[i]
template <typename T, int OP>
static inline void k_vv(const T* __restrict a, const T* __restrict b,
                        T* __restrict d, int64_t n) {
    LMD_VEC_LOOP
    for (int64_t i = 0; i < n; i++) {
        if      constexpr (OP == 0) d[i] = a[i] + b[i];
        else if constexpr (OP == 1) d[i] = a[i] - b[i];
        else if constexpr (OP == 2) d[i] = a[i] * b[i];
        else if constexpr (OP == 3) d[i] = a[i] / b[i];
    }
}

// d[i] = a[i] OP s   (vector ⊕ scalar)
template <typename T, int OP>
static inline void k_vs(const T* __restrict a, T s, T* __restrict d, int64_t n) {
    LMD_VEC_LOOP
    for (int64_t i = 0; i < n; i++) {
        if      constexpr (OP == 0) d[i] = a[i] + s;
        else if constexpr (OP == 1) d[i] = a[i] - s;
        else if constexpr (OP == 2) d[i] = a[i] * s;
        else if constexpr (OP == 3) d[i] = a[i] / s;
    }
}

// d[i] = s OP a[i]   (scalar ⊕ vector — direction matters for sub/div)
template <typename T, int OP>
static inline void k_sv(T s, const T* __restrict a, T* __restrict d, int64_t n) {
    LMD_VEC_LOOP
    for (int64_t i = 0; i < n; i++) {
        if      constexpr (OP == 0) d[i] = s + a[i];
        else if constexpr (OP == 1) d[i] = s - a[i];
        else if constexpr (OP == 2) d[i] = s * a[i];
        else if constexpr (OP == 3) d[i] = s / a[i];
    }
}

// widening kernel: int64 dst = (T)a OP (T)b, preserving Lambda's non-wrapping
// integer-widening semantics for small int/uint element types.  The widening
// load + native op auto-vectorizes; the int64 result type is unchanged from the
// existing per-element-double path it replaces.
template <typename T, int OP>
static inline void k_vv_widen(const T* __restrict a, const T* __restrict b,
                              int64_t* __restrict d, int64_t n) {
    LMD_VEC_LOOP
    for (int64_t i = 0; i < n; i++) {
        if      constexpr (OP == 0) d[i] = (int64_t)a[i] + (int64_t)b[i];
        else if constexpr (OP == 1) d[i] = (int64_t)a[i] - (int64_t)b[i];
        else if constexpr (OP == 2) d[i] = (int64_t)a[i] * (int64_t)b[i];
    }
}

// element-type predicates for selecting the contiguous same-type fast paths.
static inline bool is_double_elem(ArrayNumElemType et) {
    return et == ELEM_FLOAT || et == ELEM_FLOAT64;  // both store double (union-aliased)
}
static inline bool is_small_int_elem(ArrayNumElemType et) {
    return et == ELEM_INT8 || et == ELEM_INT16 || et == ELEM_INT32 ||
           et == ELEM_UINT8 || et == ELEM_UINT8_CLAMPED ||
           et == ELEM_UINT16 || et == ELEM_UINT32;
}

//==============================================================================
// Vector-Scalar Operations (scalar op vector, or vector op scalar)
//==============================================================================

// Generic template for scalar-vector binary operation
// op: 0=add, 1=sub, 2=mul, 3=div, 4=mod, 5=pow
// forward decl — defined after vec_scalar_op
static Item vec_broadcast_op(ArrayNum* a, ArrayNum* b, int op);

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

    // N-D arrays: wrap scalar as a 1-element ArrayNum and route through the
    // broadcast op so the result preserves the input's shape (e.g. mat + 10
    // → same shape as mat).  The 1-element wrapper broadcasts to all positions.
    if (vec_type == LMD_TYPE_ARRAY_NUM && vec.array_num->is_ndim) {
        bool sint = is_scalar_numeric(scalar_type) && scalar_type != LMD_TYPE_FLOAT;
        ArrayNum* swrap = array_num_new(sint ? ELEM_INT64 : ELEM_FLOAT, 1);
        if (!swrap) return ItemError;
        if (sint) swrap->items[0] = (int64_t)scalar_val;
        else      swrap->float_items[0] = scalar_val;
        return scalar_first ? vec_broadcast_op(swrap, vec.array_num, op)
                            : vec_broadcast_op(vec.array_num, swrap, op);
    }

    // determine result type
    bool use_float = needs_float_result(vec, scalar) || op == 3; // div always float

    // fast path for ELEM_INT64 / ELEM_INT arrays (both store in `items`).
    // Branch-hoisted: op selected outside the loop so each case is a vectorizable
    // kernel; mod (op 4) keeps its div-by-zero guard on a scalar loop.
    if (vec_type == LMD_TYPE_ARRAY_NUM && !use_float && op != 3 && op != 5 &&
        (vec.array_num->get_elem_type() == ELEM_INT64 || vec.array_num->get_elem_type() == ELEM_INT)) {
        ArrayNumElemType et = vec.array_num->get_elem_type();
        const int64_t* __restrict src = vec.array_num->items;
        int64_t sval = (int64_t)scalar_val;
        ArrayNum* result = (et == ELEM_INT64) ? array_int64_new(len) : array_num_new(ELEM_INT, len);
        int64_t* __restrict dst = result->items;
        switch (op) {
            case 0: k_vs<int64_t, 0>(src, sval, dst, len); break;  // commutative
            case 1: if (scalar_first) k_sv<int64_t, 1>(sval, src, dst, len);
                    else              k_vs<int64_t, 1>(src, sval, dst, len); break;
            case 2: k_vs<int64_t, 2>(src, sval, dst, len); break;  // commutative
            case 4: for (int64_t i = 0; i < len; i++) {
                        int64_t elem = src[i];
                        dst[i] = scalar_first ? (elem != 0 ? sval % elem : 0)
                                              : (sval != 0 ? elem % sval : 0);
                    } break;
            default: for (int64_t i = 0; i < len; i++) dst[i] = src[i]; break;
        }
        return { .array_num = result };
    }

    // fast path for ELEM_FLOAT32 compact arrays — branch-hoisted, __restrict
    // kernels for add/sub/mul/div; fmod/pow stay on scalar loops.
    if (vec_type == LMD_TYPE_ARRAY_NUM && vec.array_num->get_elem_type() == ELEM_FLOAT32) {
        const float* __restrict src = (const float*)vec.array_num->data;
        float sval = (float)scalar_val;
        ArrayNum* result = array_num_new(ELEM_FLOAT32, len);
        float* __restrict dst = (float*)result->data;
        switch (op) {
            case 0: k_vs<float, 0>(src, sval, dst, len); break;
            case 1: if (scalar_first) k_sv<float, 1>(sval, src, dst, len);
                    else              k_vs<float, 1>(src, sval, dst, len); break;
            case 2: k_vs<float, 2>(src, sval, dst, len); break;
            case 3: if (scalar_first) k_sv<float, 3>(sval, src, dst, len);
                    else              k_vs<float, 3>(src, sval, dst, len); break;
            case 4: for (int64_t i = 0; i < len; i++) {
                        float a = scalar_first ? sval : src[i], b = scalar_first ? src[i] : sval;
                        dst[i] = fmodf(a, b);
                    } break;
            case 5: for (int64_t i = 0; i < len; i++) {
                        float a = scalar_first ? sval : src[i], b = scalar_first ? src[i] : sval;
                        dst[i] = powf(a, b);
                    } break;
            default: for (int64_t i = 0; i < len; i++) dst[i] = src[i]; break;
        }
        return { .array_num = result };
    }

    // fast path for double-storage arrays (ELEM_FLOAT / ELEM_FLOAT64) — branch-hoisted,
    // __restrict kernels for add/sub/mul/div; fmod/pow stay on scalar loops.
    if (vec_type == LMD_TYPE_ARRAY_NUM && is_double_elem(vec.array_num->get_elem_type())) {
        const double* __restrict src = (const double*)vec.array_num->data;
        double sval = scalar_val;
        ArrayNum* result = array_float_new(len);
        double* __restrict dst = result->float_items;
        switch (op) {
            case 0: k_vs<double, 0>(src, sval, dst, len); break;
            case 1: if (scalar_first) k_sv<double, 1>(sval, src, dst, len);
                    else              k_vs<double, 1>(src, sval, dst, len); break;
            case 2: k_vs<double, 2>(src, sval, dst, len); break;
            case 3: if (scalar_first) k_sv<double, 3>(sval, src, dst, len);
                    else              k_vs<double, 3>(src, sval, dst, len); break;
            case 4: for (int64_t i = 0; i < len; i++) {
                        double a = scalar_first ? sval : src[i], b = scalar_first ? src[i] : sval;
                        dst[i] = fmod(a, b);
                    } break;
            case 5: for (int64_t i = 0; i < len; i++) {
                        double a = scalar_first ? sval : src[i], b = scalar_first ? src[i] : sval;
                        dst[i] = pow(a, b);
                    } break;
            default: for (int64_t i = 0; i < len; i++) dst[i] = src[i]; break;
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
// N-D Shape Broadcasting
//==============================================================================
//
// NumPy-style rules:
//   - Right-align dimensions; pad shorter operand with leading 1s.
//   - At each axis, dims must be equal OR one of them must be 1.
//   - A size-1 dim is "stretched" by setting its effective stride to 0,
//     so the same element is read for all indices on that axis.
//
// Element access uses pre-adjusted data pointers — owned 1-D arrays start at
// base->data; subviews carry offset baked into their data pointer; reshape
// keeps data = base->data with stride metadata.  So `data[flat_offset]` always
// resolves correctly when flat_offset is computed via effective strides.

// Read shape and strides into caller-provided arrays. Returns ndim.
// For 1-D owned arrays, returns ndim=1 with shp[0]=length, str[0]=1.
static int get_shape_strides(ArrayNum* arr, int64_t* shp, int64_t* str) {
    if (arr->is_ndim && arr->extra) {
        ArrayNumShape* s = (ArrayNumShape*)(uintptr_t)arr->extra;
        int64_t* sd = array_num_shape_dims(s);
        int64_t* ss = array_num_shape_strides(s);
        for (int i = 0; i < s->ndim; i++) { shp[i] = sd[i]; str[i] = ss[i]; }
        return s->ndim;
    }
    shp[0] = arr->length;
    str[0] = 1;
    return 1;
}

// Compute broadcast output shape from two operand shapes.
// Outputs broadcast shape into out_shp[] and effective strides for each operand
// into eff_str_a[] and eff_str_b[] (with 0 for stretched size-1 axes).
// Returns output ndim, or -1 if shapes are incompatible.
static int compute_broadcast_shape(
    int ndim_a, const int64_t* shp_a, const int64_t* str_a,
    int ndim_b, const int64_t* shp_b, const int64_t* str_b,
    int64_t* out_shp, int64_t* eff_str_a, int64_t* eff_str_b) {
    int out_ndim = (ndim_a > ndim_b) ? ndim_a : ndim_b;
    for (int axis = 0; axis < out_ndim; axis++) {
        // right-align: out axis 0 = leading; align from the right
        int a_axis = ndim_a - out_ndim + axis;
        int b_axis = ndim_b - out_ndim + axis;
        int64_t dim_a = (a_axis >= 0) ? shp_a[a_axis] : 1;
        int64_t dim_b = (b_axis >= 0) ? shp_b[b_axis] : 1;
        int64_t stride_a = (a_axis >= 0) ? str_a[a_axis] : 0;
        int64_t stride_b = (b_axis >= 0) ? str_b[b_axis] : 0;
        if (dim_a == dim_b) {
            out_shp[axis] = dim_a;
            eff_str_a[axis] = stride_a;
            eff_str_b[axis] = stride_b;
        } else if (dim_a == 1) {
            out_shp[axis] = dim_b;
            eff_str_a[axis] = 0;        // stretched
            eff_str_b[axis] = stride_b;
        } else if (dim_b == 1) {
            out_shp[axis] = dim_a;
            eff_str_a[axis] = stride_a;
            eff_str_b[axis] = 0;        // stretched
        } else {
            return -1;  // incompatible: dims differ and neither is 1
        }
    }
    return out_ndim;
}

// Read one numeric element from an ArrayNum at a flat-byte offset (in elements).
// Handles all ELEM_* element kinds, converting to double for uniform processing.
static inline double read_arr_elem_as_double(ArrayNum* arr, int64_t off) {
    switch (arr->get_elem_type()) {
        case ELEM_INT:   case ELEM_INT64:  return (double)arr->items[off];
        case ELEM_FLOAT: case ELEM_FLOAT64: return arr->float_items[off];
        case ELEM_INT8:    return (double)((int8_t*)arr->data)[off];
        case ELEM_INT16:   return (double)((int16_t*)arr->data)[off];
        case ELEM_INT32:   return (double)((int32_t*)arr->data)[off];
        case ELEM_UINT8:   return (double)((uint8_t*)arr->data)[off];
        case ELEM_UINT8_CLAMPED: return (double)((uint8_t*)arr->data)[off];
        case ELEM_UINT16:  return (double)((uint16_t*)arr->data)[off];
        case ELEM_UINT32:  return (double)((uint32_t*)arr->data)[off];
        case ELEM_FLOAT32: return (double)((float*)arr->data)[off];
        case ELEM_UINT64:  return (double)((uint64_t*)arr->data)[off];
        case ELEM_BOOL:    return ((uint8_t*)arr->data)[off] ? 1.0 : 0.0;
        default:           return 0.0;
    }
}

static inline uint8_t clamp_uint8_even(double value) {
    if (isnan(value) || value <= 0.0) return 0;
    if (value >= 255.0) return 255;

    double lower;
    double frac = modf(value, &lower);
    long rounded = (long)lower;
    if (frac > 0.5) {
        rounded++;
    } else if (frac == 0.5 && (rounded & 1)) {
        rounded++;
    }
    return (uint8_t)rounded;
}

// Write a double into element `off`, rounding + clamping to the elem type's range
// (the counterpart to read_arr_elem_as_double).  Float types store the value raw.
static inline void write_arr_elem_from_double(ArrayNum* arr, int64_t off, double v) {
    switch (arr->get_elem_type()) {
        case ELEM_INT:   case ELEM_INT64:   arr->items[off] = (int64_t)llround(v); return;
        case ELEM_FLOAT: case ELEM_FLOAT64: arr->float_items[off] = v; return;
        case ELEM_FLOAT32: ((float*)arr->data)[off] = (float)v; return;
        case ELEM_INT8:   { long r = llround(v); ((int8_t*)arr->data)[off]   = (int8_t)(r < -128 ? -128 : r > 127 ? 127 : r); return; }
        case ELEM_UINT8:  { long r = llround(v); ((uint8_t*)arr->data)[off]  = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r); return; }
        case ELEM_UINT8_CLAMPED: ((uint8_t*)arr->data)[off] = clamp_uint8_even(v); return;
        case ELEM_INT16:  { long r = llround(v); ((int16_t*)arr->data)[off]  = (int16_t)(r < -32768 ? -32768 : r > 32767 ? 32767 : r); return; }
        case ELEM_UINT16: { long r = llround(v); ((uint16_t*)arr->data)[off] = (uint16_t)(r < 0 ? 0 : r > 65535 ? 65535 : r); return; }
        case ELEM_INT32:  ((int32_t*)arr->data)[off]  = (int32_t)llround(v); return;
        case ELEM_UINT32: { long long r = llround(v); ((uint32_t*)arr->data)[off] = (uint32_t)(r < 0 ? 0 : r); return; }
        case ELEM_UINT64: ((uint64_t*)arr->data)[off] = (uint64_t)(v < 0 ? 0 : v); return;
        case ELEM_BOOL:   ((uint8_t*)arr->data)[off] = (v != 0.0) ? 1 : 0; return;
        default:          return;
    }
}

// Returns true if elem_type stores integers (not floats).
static inline bool elem_is_int(ArrayNumElemType e) {
    switch (e) {
        case ELEM_FLOAT: case ELEM_FLOAT16: case ELEM_FLOAT32: case ELEM_FLOAT64:
            return false;
        default: return true;
    }
}

// Allocate an N-D ArrayNum with the given shape and elem_type, plus shape side-table.
// Used to materialize broadcast op results.
static ArrayNum* alloc_ndim_arraynum(ArrayNumElemType etype, int ndim, const int64_t* shape) {
    int64_t total = 1;
    for (int i = 0; i < ndim; i++) total *= shape[i];
    ArrayNum* arr = array_num_new(etype, total);
    if (!arr || ndim == 1) return arr;
    // attach shape side-table
    size_t shape_bytes = sizeof(ArrayNumShape) + 2 * (size_t)ndim * sizeof(int64_t);
    ArrayNumShape* s = (ArrayNumShape*)heap_data_calloc(shape_bytes);
    if (!s) return arr;  // best-effort: keep as 1-D
    s->ndim = (uint8_t)ndim;
    s->is_c_contig = 1;
    s->is_f_contig = (ndim == 1) ? 1 : 0;
    s->offset = 0;
    s->base = NULL;  // owned, no base
    int64_t* sd = array_num_shape_dims(s);
    int64_t* ss = array_num_shape_strides(s);
    int64_t stride = 1;
    for (int i = ndim - 1; i >= 0; i--) {
        sd[i] = shape[i];
        ss[i] = stride;
        stride *= shape[i];
    }
    arr->is_ndim = 1;
    arr->extra = (int64_t)(uintptr_t)s;
    return arr;
}

// Broadcast binary op: walk output shape with strided iterator, fetch from each
// operand via effective strides, compute op, store. Uses double internally; for
// int-result paths, casts back to int64 at the end.
static Item vec_broadcast_op(ArrayNum* a, ArrayNum* b, int op) {
    int64_t shp_a[32], str_a[32], shp_b[32], str_b[32];
    int ndim_a = get_shape_strides(a, shp_a, str_a);
    int ndim_b = get_shape_strides(b, shp_b, str_b);

    int64_t out_shp[32], eff_a[32], eff_b[32];
    int out_ndim = compute_broadcast_shape(ndim_a, shp_a, str_a, ndim_b, shp_b, str_b,
                                            out_shp, eff_a, eff_b);
    if (out_ndim < 0) {
        // build a small diagnostic of the two shapes
        log_error("vec_broadcast_op: incompatible shapes for op %d (ndim %d vs %d)",
                  op, ndim_a, ndim_b);
        return ItemError;
    }

    int64_t total = 1;
    for (int i = 0; i < out_ndim; i++) total *= out_shp[i];

    // Result element type: float if either operand is float OR op is div/pow
    bool result_is_float = !elem_is_int(a->get_elem_type()) ||
                           !elem_is_int(b->get_elem_type()) ||
                           op == 3 || op == 5;
    ArrayNumElemType result_etype = result_is_float ? ELEM_FLOAT : ELEM_INT64;
    ArrayNum* result = alloc_ndim_arraynum(result_etype, out_ndim, out_shp);
    if (!result) return ItemError;

    // Walk all output positions using an N-D index counter.
    int64_t idx[32] = {0};
    for (int64_t k = 0; k < total; k++) {
        // compute operand offsets via dot product of current idx with effective strides
        int64_t off_a = 0, off_b = 0;
        for (int axis = 0; axis < out_ndim; axis++) {
            off_a += idx[axis] * eff_a[axis];
            off_b += idx[axis] * eff_b[axis];
        }
        double va = read_arr_elem_as_double(a, off_a);
        double vb = read_arr_elem_as_double(b, off_b);
        double res;
        switch (op) {
            case 0: res = va + vb; break;
            case 1: res = va - vb; break;
            case 2: res = va * vb; break;
            case 3: res = va / vb; break;
            case 4: res = fmod(va, vb); break;
            case 5: res = pow(va, vb); break;
            default: res = NAN; break;
        }
        if (result_is_float) result->float_items[k] = res;
        else                  result->items[k] = (int64_t)res;

        // increment N-D index counter (last axis fastest)
        for (int axis = out_ndim - 1; axis >= 0; axis--) {
            idx[axis]++;
            if (idx[axis] < out_shp[axis]) break;
            idx[axis] = 0;
        }
    }
    return { .array_num = result };
}

// ============================================================================
// Vectorized comparisons: a OP b (a > 0, a == b, …) → ELEM_BOOL mask array.
// op codes are (operator - OPERATOR_EQ): 0=EQ 1=NE 2=LT 3=LE 4=GT 5=GE.
// Used to produce boolean masks for arr[mask] indexing.
// ============================================================================
static inline bool cmp_apply(double a, double b, int op) {
    switch (op) {
        case 0: return a == b;
        case 1: return a != b;
        case 2: return a <  b;
        case 3: return a <= b;
        case 4: return a >  b;
        case 5: return a >= b;
    }
    return false;
}

// element-wise comparison of two typed arrays (NumPy broadcast) → ELEM_BOOL array
static Item vec_cmp_broadcast(ArrayNum* a, ArrayNum* b, int op) {
    int64_t shp_a[32], str_a[32], shp_b[32], str_b[32];
    int ndim_a = get_shape_strides(a, shp_a, str_a);
    int ndim_b = get_shape_strides(b, shp_b, str_b);
    int64_t out_shp[32], eff_a[32], eff_b[32];
    int out_ndim = compute_broadcast_shape(ndim_a, shp_a, str_a, ndim_b, shp_b, str_b,
                                           out_shp, eff_a, eff_b);
    if (out_ndim < 0) { log_error("vec_cmp: incompatible shapes"); return ItemError; }
    int64_t total = 1;
    for (int i = 0; i < out_ndim; i++) total *= out_shp[i];
    ArrayNum* result = alloc_ndim_arraynum(ELEM_BOOL, out_ndim, out_shp);
    if (!result) return ItemError;
    uint8_t* out = (uint8_t*)result->data;
    int64_t idx[32] = {0};
    for (int64_t k = 0; k < total; k++) {
        int64_t oa = 0, ob = 0;
        for (int ax = 0; ax < out_ndim; ax++) { oa += idx[ax] * eff_a[ax]; ob += idx[ax] * eff_b[ax]; }
        out[k] = cmp_apply(read_arr_elem_as_double(a, oa), read_arr_elem_as_double(b, ob), op) ? 1 : 0;
        for (int ax = out_ndim - 1; ax >= 0; ax--) { idx[ax]++; if (idx[ax] < out_shp[ax]) break; idx[ax] = 0; }
    }
    return { .array_num = result };
}

// a OP b where at least one operand is a typed numeric array → bool mask.
Item vec_cmp(Item a, Item b, int op) {
    GUARD_ERROR2(a, b);
    bool a_arr = (get_type_id(a) == LMD_TYPE_ARRAY_NUM);
    bool b_arr = (get_type_id(b) == LMD_TYPE_ARRAY_NUM);
    if (a_arr && b_arr) return vec_cmp_broadcast(a.array_num, b.array_num, op);
    if (a_arr || b_arr) {
        // array vs scalar: wrap the scalar as a 1-element array and broadcast
        Item scalar = a_arr ? b : a;
        TypeId st = get_type_id(scalar);
        if (!is_scalar_numeric(st)) {
            log_error("vec_cmp: cannot compare array with non-numeric %s", get_type_name(st));
            return ItemError;
        }
        double sv = item_to_double(scalar);
        bool sint = (st != LMD_TYPE_FLOAT);
        ArrayNum* sw = array_num_new(sint ? ELEM_INT64 : ELEM_FLOAT, 1);
        if (!sw) return ItemError;
        if (sint) sw->items[0] = (int64_t)sv; else sw->float_items[0] = sv;
        return a_arr ? vec_cmp_broadcast(a.array_num, sw, op)
                     : vec_cmp_broadcast(sw, b.array_num, op);
    }
    return ItemError;  // neither operand is an array (transpiler should not route here)
}

// True iff either ArrayNum carries n-d shape metadata.
static inline bool either_is_ndim(ArrayNum* a, ArrayNum* b) {
    return (a && a->is_ndim) || (b && b->is_ndim);
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

    // N-D shape broadcasting: when either operand carries shape metadata, dispatch
    // through the broadcast path which respects shape rules (right-align, size-1
    // dims stretch).  Falls back to length match for the all-1-D case.
    if (type_a == LMD_TYPE_ARRAY_NUM && type_b == LMD_TYPE_ARRAY_NUM) {
        ArrayNum* a = vec_a.array_num;
        ArrayNum* b = vec_b.array_num;
        if (either_is_ndim(a, b)) {
            return vec_broadcast_op(a, b, op);
        }
    }

    // single-element broadcasting (1-D length-1)
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

    // fast path: both ELEM_INT64 or both ELEM_INT (same type, stored in `items`).
    // Branch-hoisted: op selected outside the loop; mod keeps its scalar guard.
    if (type_a == LMD_TYPE_ARRAY_NUM && type_b == LMD_TYPE_ARRAY_NUM &&
        !use_float && op != 3 && op != 5 &&
        vec_a.array_num->get_elem_type() == vec_b.array_num->get_elem_type() &&
        (vec_a.array_num->get_elem_type() == ELEM_INT64 || vec_a.array_num->get_elem_type() == ELEM_INT)) {
        ArrayNumElemType et = vec_a.array_num->get_elem_type();
        const int64_t* __restrict a = vec_a.array_num->items;
        const int64_t* __restrict b = vec_b.array_num->items;
        ArrayNum* result = (et == ELEM_INT64) ? array_int64_new(len) : array_num_new(ELEM_INT, len);
        int64_t* __restrict dst = result->items;
        switch (op) {
            case 0: k_vv<int64_t, 0>(a, b, dst, len); break;
            case 1: k_vv<int64_t, 1>(a, b, dst, len); break;
            case 2: k_vv<int64_t, 2>(a, b, dst, len); break;
            case 4: for (int64_t i = 0; i < len; i++) dst[i] = b[i] != 0 ? a[i] % b[i] : 0; break;
            default: for (int64_t i = 0; i < len; i++) dst[i] = a[i]; break;
        }
        return { .array_num = result };
    }

    // fast path: both ELEM_FLOAT32 — branch-hoisted, __restrict kernels for
    // add/sub/mul/div; fmod/pow stay on scalar loops.
    if (type_a == LMD_TYPE_ARRAY_NUM && vec_a.array_num->get_elem_type() == ELEM_FLOAT32 &&
        type_b == LMD_TYPE_ARRAY_NUM && vec_b.array_num->get_elem_type() == ELEM_FLOAT32) {
        const float* __restrict sa = (const float*)vec_a.array_num->data;
        const float* __restrict sb = (const float*)vec_b.array_num->data;
        ArrayNum* result = array_num_new(ELEM_FLOAT32, len);
        float* __restrict dst = (float*)result->data;
        switch (op) {
            case 0: k_vv<float, 0>(sa, sb, dst, len); break;
            case 1: k_vv<float, 1>(sa, sb, dst, len); break;
            case 2: k_vv<float, 2>(sa, sb, dst, len); break;
            case 3: k_vv<float, 3>(sa, sb, dst, len); break;
            case 4: for (int64_t i = 0; i < len; i++) dst[i] = fmodf(sa[i], sb[i]); break;
            case 5: for (int64_t i = 0; i < len; i++) dst[i] = powf(sa[i], sb[i]); break;
            default: for (int64_t i = 0; i < len; i++) dst[i] = sa[i]; break;
        }
        return { .array_num = result };
    }

    // fast path: both double-storage (ELEM_FLOAT / ELEM_FLOAT64) — branch-hoisted,
    // __restrict kernels for add/sub/mul/div; fmod/pow stay on scalar loops.
    if (type_a == LMD_TYPE_ARRAY_NUM && type_b == LMD_TYPE_ARRAY_NUM &&
        is_double_elem(vec_a.array_num->get_elem_type()) &&
        is_double_elem(vec_b.array_num->get_elem_type())) {
        const double* __restrict sa = (const double*)vec_a.array_num->data;
        const double* __restrict sb = (const double*)vec_b.array_num->data;
        ArrayNum* result = array_float_new(len);
        double* __restrict dst = result->float_items;
        switch (op) {
            case 0: k_vv<double, 0>(sa, sb, dst, len); break;
            case 1: k_vv<double, 1>(sa, sb, dst, len); break;
            case 2: k_vv<double, 2>(sa, sb, dst, len); break;
            case 3: k_vv<double, 3>(sa, sb, dst, len); break;
            case 4: for (int64_t i = 0; i < len; i++) dst[i] = fmod(sa[i], sb[i]); break;
            case 5: for (int64_t i = 0; i < len; i++) dst[i] = pow(sa[i], sb[i]); break;
            default: for (int64_t i = 0; i < len; i++) dst[i] = sa[i]; break;
        }
        return { .array_num = result };
    }

    // fast path: both the same small int/uint type — native contiguous read,
    // widening to an int64 result.  Matches the per-element-double path's
    // non-wrapping semantics exactly, but the widening load + native op vectorizes.
    if (type_a == LMD_TYPE_ARRAY_NUM && type_b == LMD_TYPE_ARRAY_NUM &&
        !use_float && op != 3 && op != 5 &&
        vec_a.array_num->get_elem_type() == vec_b.array_num->get_elem_type() &&
        is_small_int_elem(vec_a.array_num->get_elem_type())) {
        ArrayNumElemType et = vec_a.array_num->get_elem_type();
        const void* pa = vec_a.array_num->data;
        const void* pb = vec_b.array_num->data;
        ArrayNum* result = array_int64_new(len);
        int64_t* __restrict dst = result->items;
        #define LMD_WIDEN_VV(CT) \
            switch (op) { \
                case 0: k_vv_widen<CT, 0>((const CT*)pa, (const CT*)pb, dst, len); break; \
                case 1: k_vv_widen<CT, 1>((const CT*)pa, (const CT*)pb, dst, len); break; \
                case 2: k_vv_widen<CT, 2>((const CT*)pa, (const CT*)pb, dst, len); break; \
                case 4: { const CT* a = (const CT*)pa; const CT* b = (const CT*)pb; \
                          for (int64_t i = 0; i < len; i++) { int64_t bi = (int64_t)b[i]; \
                              dst[i] = bi != 0 ? (int64_t)a[i] % bi : 0; } } break; \
                default: { const CT* a = (const CT*)pa; \
                           for (int64_t i = 0; i < len; i++) dst[i] = (int64_t)a[i]; } break; \
            }
        switch (et) {
            case ELEM_INT8:   LMD_WIDEN_VV(int8_t);   break;
            case ELEM_INT16:  LMD_WIDEN_VV(int16_t);  break;
            case ELEM_INT32:  LMD_WIDEN_VV(int32_t);  break;
            case ELEM_UINT8:  LMD_WIDEN_VV(uint8_t);  break;
            case ELEM_UINT8_CLAMPED: LMD_WIDEN_VV(uint8_t); break;
            case ELEM_UINT16: LMD_WIDEN_VV(uint16_t); break;
            case ELEM_UINT32: LMD_WIDEN_VV(uint32_t); break;
            default: break;
        }
        #undef LMD_WIDEN_VV
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

static bool is_skip_null_option(Item option) {
    return get_type_id(option) == LMD_TYPE_BOOL && option.bool_val;
}

static bool vector_number_or_null(Item item, double* out, bool* is_null) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_NULL) {
        *is_null = true;
        return true;
    }
    *is_null = false;
    double val = item_to_double(item);
    if (std::isnan(val)) return false;
    *out = val;
    return true;
}

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
            double val = 0.0;
            bool is_null = false;
            if (!vector_number_or_null(elem, &val, &is_null)) {
                log_error("fn_math_prod: non-numeric element at index %ld", i);
                return ItemError;
            }
            // null in aggregate inputs is an unknown value; propagate it in strict aggregates.
            if (is_null) return ItemNull;
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
        result->length = 0;  result->extra = 0;  result->capacity = 0;
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
        result->length = n;  result->extra = 0;  result->capacity = n;
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

Item fn_math_mean_skip_null(Item item, bool skip_null) {
    GUARD_ERROR1(item);
    extern Item fn_avg_skip_null(Item, bool);
    return fn_avg_skip_null(item, skip_null);
}

// median(vec) - median value
Item fn_math_median_skip_null(Item item, bool skip_null) {
    GUARD_ERROR1(item);
    int64_t len = vector_length(item);
    if (len <= 0) return ItemNull;

    // Copy values to sortable array
    ArrayNum* sorted = array_float_new(len);
    int64_t count = 0;
    for (int64_t i = 0; i < len; i++) {
        double val = 0.0;
        bool is_null = false;
        if (!vector_number_or_null(vector_get(item, i), &val, &is_null)) {
            log_error("fn_math_median: non-numeric element at index %ld", i);
            return ItemError;
        }
        // null is an absence marker; strict aggregates propagate it, skip_null removes it.
        if (is_null) {
            if (skip_null) continue;
            return ItemNull;
        }
        sorted->float_items[count++] = val;
    }
    if (count == 0) return ItemNull;

    insertion_sort(sorted->float_items, (size_t)count, sizeof(double), cmp_double_asc, NULL);

    if (count % 2 == 1) {
        return push_d(sorted->float_items[count / 2]);
    } else {
        return push_d((sorted->float_items[count / 2 - 1] + sorted->float_items[count / 2]) / 2.0);
    }
}

Item fn_math_median(Item item) {
    return fn_math_median_skip_null(item, false);
}

// variance(vec) - population variance
Item fn_math_variance_skip_null(Item item, bool skip_null) {
    GUARD_ERROR1(item);
    int64_t len = vector_length(item);
    if (len <= 0) return ItemNull;

    // Calculate mean
    double sum = 0.0;
    int64_t count = 0;
    for (int64_t i = 0; i < len; i++) {
        double val = 0.0;
        bool is_null = false;
        if (!vector_number_or_null(vector_get(item, i), &val, &is_null)) {
            log_error("fn_math_variance: non-numeric element at index %ld", i);
            return ItemError;
        }
        // null is an absence marker; strict aggregates propagate it, skip_null removes it.
        if (is_null) {
            if (skip_null) continue;
            return ItemNull;
        }
        sum += val;
        count++;
    }
    if (count == 0) return ItemNull;
    double mean = sum / count;

    // Calculate variance
    double var_sum = 0.0;
    for (int64_t i = 0; i < len; i++) {
        double val = 0.0;
        bool is_null = false;
        if (!vector_number_or_null(vector_get(item, i), &val, &is_null)) {
            log_error("fn_math_variance: non-numeric element at index %ld", i);
            return ItemError;
        }
        if (is_null) continue;
        double diff = val - mean;
        var_sum += diff * diff;
    }

    return push_d(var_sum / count);
}

Item fn_math_variance(Item item) {
    return fn_math_variance_skip_null(item, false);
}

// deviation(vec) - population standard deviation
Item fn_math_deviation_skip_null(Item item, bool skip_null) {
    GUARD_ERROR1(item);
    Item var_result = fn_math_variance_skip_null(item, skip_null);
    if (get_type_id(var_result) == LMD_TYPE_ERROR) return var_result;
    if (get_type_id(var_result) == LMD_TYPE_NULL) return var_result;

    double variance = var_result.get_double();
    return push_d(sqrt(variance));
}

Item fn_math_deviation(Item item) {
    return fn_math_deviation_skip_null(item, false);
}

// quantile(vec, p) - p-th quantile (0 <= p <= 1)
Item fn_math_quantile_skip_null(Item item, Item p_item, bool skip_null) {
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
    int64_t count = 0;
    for (int64_t i = 0; i < len; i++) {
        double val = 0.0;
        bool is_null = false;
        if (!vector_number_or_null(vector_get(item, i), &val, &is_null)) {
            log_error("fn_math_quantile: non-numeric element at index %ld", i);
            return ItemError;
        }
        // null is an absence marker; strict aggregates propagate it, skip_null removes it.
        if (is_null) {
            if (skip_null) continue;
            return ItemNull;
        }
        sorted->float_items[count++] = val;
    }
    if (count == 0) return ItemNull;

    // Sort
    for (int64_t i = 0; i < count - 1; i++) {
        for (int64_t j = 0; j < count - i - 1; j++) {
            if (sorted->float_items[j] > sorted->float_items[j + 1]) {
                double tmp = sorted->float_items[j];
                sorted->float_items[j] = sorted->float_items[j + 1];
                sorted->float_items[j + 1] = tmp;
            }
        }
    }

    // Linear interpolation method (same as NumPy's default)
    double idx = p * (count - 1);
    int64_t lo = (int64_t)floor(idx);
    int64_t hi = (int64_t)ceil(idx);

    if (lo == hi || hi >= count) {
        return push_d(sorted->float_items[lo]);
    }

    double frac = idx - lo;
    return push_d(sorted->float_items[lo] * (1.0 - frac) + sorted->float_items[hi] * frac);
}

Item fn_math_quantile(Item item, Item p_item) {
    return fn_math_quantile_skip_null(item, p_item, false);
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
        bool preserve_array = type == LMD_TYPE_ARRAY && !item.array->is_spreadable;
        List* result = list();
        for (int64_t i = len - 1; i >= 0; i--) {
            list_push(result, vector_get(item, i));
        }
        // sort() returns a plain non-spreadable array; reverse() must preserve that
        // container mode so method chains keep bracketed array output.
        if (preserve_array) result->is_spreadable = false;
        else result->is_content = 1;
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

    Array* result = vector_to_plain_array(item, len);
    stable_sort_items_by_total_order(result->items, len, false);
    return { .array = result };
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

    // build index permutation array; keys stay as Items so sort/order by share total order.
    int64_t* indices = (int64_t*)mem_calloc(len, sizeof(int64_t), MEM_CAT_EVAL);
    for (int64_t i = 0; i < len; i++) {
        indices[i] = i;
    }

    // stable bubble sort indices by total key order
    for (int64_t i = 0; i < len - 1; i++) {
        for (int64_t j = 0; j < len - i - 1; j++) {
            int cmp = total_cmp(vector_get(keys, indices[j]), vector_get(keys, indices[j + 1]));
            bool swap = descending ? (cmp < 0) : (cmp > 0);
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
                int cmp = total_cmp(key_vals[indices[j]], key_vals[indices[j + 1]]);
                bool swap = descending ? (cmp < 0) : (cmp > 0);
                if (swap) {
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

    Array* result = vector_to_plain_array(item, len);
    stable_sort_items_by_total_order(result->items, len, descending);
    return { .array = result };
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

    int64_t start = 0;
    int64_t end = 0;
    if (!item_to_integral_index(start_item, &start) ||
        !item_to_integral_index(end_item, &end)) {
        log_error("fn_slice: start and end must be integer-valued numbers");
        return ItemError;
    }

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

Item fn_slice3(Item vec, Item start_item, Item end_item) {
    return fn_slice(vec, start_item, end_item);
}

// slice(vec, start) - extract elements from start to the end
Item fn_slice2(Item vec, Item start_item) {
    GUARD_ERROR2(vec, start_item);
    if (get_type_id(vec) == LMD_TYPE_NULL) return ItemNull;

    int64_t len = fn_len(vec);
    if (len < 0) {
        log_error("fn_slice2: first argument must be sliceable");
        return ItemError;
    }
    return fn_slice(vec, start_item, (Item){ .item = i2it(len) });
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
    view->is_mutable_view = 1;  // writable through to base in procedural code (Scope 3)
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
    view->is_mutable_view = 1;  // writable through to base in procedural code (Scope 3)
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

// ============================================================================
// N-D structural ops: transpose / flatten / ravel / matmul / concat / stack
// ============================================================================

// Is the array's data laid out C-contiguously (a flat scan preserves C-order)?
static bool arr_num_is_c_contig(ArrayNum* a) {
    if (!(a->is_ndim && a->extra)) return true;   // plain 1-D owned/subview
    return ((ArrayNumShape*)(uintptr_t)a->extra)->is_c_contig;
}

// Copy `src`'s elements into `dst` starting at flat index `dst_base`, in C-order,
// converting to dst's elem_type.  Fast path: raw memcpy when same type & contiguous.
static void arr_num_copy_into(ArrayNum* src, ArrayNum* dst, int64_t dst_base) {
    ArrayNumElemType det = dst->get_elem_type();
    if (src->get_elem_type() == det && arr_num_is_c_contig(src)) {
        int es = ELEM_TYPE_SIZE[det >> 4];
        memcpy((char*)dst->data + (size_t)dst_base * es, src->data, (size_t)src->length * es);
        return;
    }
    // per-element conversion, walking src in C-order via shape/strides
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(src, shp, str);
    int64_t idx[32] = {0};
    int64_t off = 0;
    bool dst_float = !elem_is_int(det);
    for (int64_t i = 0; i < src->length; i++) {
        double v = read_arr_elem_as_double(src, off);
        array_num_set_item(dst, dst_base + i, dst_float ? push_d(v) : push_l((int64_t)v));
        for (int ax = ndim - 1; ax >= 0; ax--) {
            idx[ax]++; off += str[ax];
            if (idx[ax] < shp[ax]) break;
            idx[ax] = 0; off -= shp[ax] * str[ax];
        }
    }
}

// transpose(arr) - view with reversed axes (zero-copy; non-contiguous result)
Item fn_transpose(Item vec) {
    GUARD_ERROR1(vec);
    if (get_type_id(vec) != LMD_TYPE_ARRAY_NUM) {
        log_error("fn_transpose: expected typed numeric array");
        return ItemError;
    }
    ArrayNum* base = vec.array_num;
    if (!base) return ItemError;
    // 1-D (or no shape table): transpose is identity
    if (!(base->is_ndim && base->extra)) return vec;
    ArrayNumShape* bs = (ArrayNumShape*)(uintptr_t)base->extra;
    int ndim = bs->ndim;
    if (ndim < 2) return vec;
    if (!base->is_heap) {
        log_error("fn_transpose: cannot view arena-backed array; copy() first");
        return ItemError;
    }
    int64_t* bshp = array_num_shape_dims(bs);
    int64_t* bstr = array_num_shape_strides(bs);

    ArrayNum* view = (ArrayNum*)heap_calloc(sizeof(ArrayNum), LMD_TYPE_ARRAY_NUM);
    if (!view) return ItemError;
    view->type_id = LMD_TYPE_ARRAY_NUM;
    view->set_elem_type(base->get_elem_type());
    view->is_ndim = 1;
    view->is_view = 1;
    view->is_mutable_view = 1;  // writable through to base in procedural code (Scope 3)
    view->data = base->data;
    view->length = base->length;
    view->capacity = base->length;

    size_t shape_bytes = sizeof(ArrayNumShape) + 2 * (size_t)ndim * sizeof(int64_t);
    ArrayNumShape* s = (ArrayNumShape*)heap_data_calloc(shape_bytes);
    if (!s) return ItemError;
    s->ndim = (uint8_t)ndim;
    s->offset = 0;
    s->base = (void*)base;
    int64_t* shp = array_num_shape_dims(s);
    int64_t* str = array_num_shape_strides(s);
    for (int i = 0; i < ndim; i++) {       // reverse axes
        shp[i] = bshp[ndim - 1 - i];
        str[i] = bstr[ndim - 1 - i];
    }
    s->is_c_contig = 0;
    s->is_f_contig = bs->is_c_contig;       // transpose of C-contig is F-contig
    view->extra = (int64_t)(uintptr_t)s;
    base->is_pinned = 1;
    return { .array_num = view };
}

// flatten(arr) - owned 1-D contiguous copy (C-order)
Item fn_flatten(Item vec) {
    GUARD_ERROR1(vec);
    if (get_type_id(vec) != LMD_TYPE_ARRAY_NUM) {
        log_error("fn_flatten: expected typed numeric array");
        return ItemError;
    }
    ArrayNum* base = vec.array_num;
    if (!base) return ItemError;
    ArrayNum* result = array_num_new(base->get_elem_type(), base->length);
    if (!result || base->length == 0) return { .array_num = result };
    arr_num_copy_into(base, result, 0);
    return { .array_num = result };
}

// ravel(arr) - 1-D view if C-contiguous (zero-copy), else a flattened copy
Item fn_ravel(Item vec) {
    GUARD_ERROR1(vec);
    if (get_type_id(vec) != LMD_TYPE_ARRAY_NUM) {
        log_error("fn_ravel: expected typed numeric array");
        return ItemError;
    }
    ArrayNum* base = vec.array_num;
    if (!base) return ItemError;
    if (!(arr_num_is_c_contig(base) && base->is_heap)) {
        return fn_flatten(vec);
    }
    // contiguous → a 1-D view with a shape side-table (carries base ref for GC)
    ArrayNum* view = (ArrayNum*)heap_calloc(sizeof(ArrayNum), LMD_TYPE_ARRAY_NUM);
    if (!view) return ItemError;
    view->type_id = LMD_TYPE_ARRAY_NUM;
    view->set_elem_type(base->get_elem_type());
    view->is_ndim = 1;
    view->is_view = 1;
    view->is_mutable_view = 1;  // writable through to base in procedural code (Scope 3)
    view->data = base->data;
    view->length = base->length;
    view->capacity = base->length;
    size_t shape_bytes = sizeof(ArrayNumShape) + 2 * sizeof(int64_t);
    ArrayNumShape* s = (ArrayNumShape*)heap_data_calloc(shape_bytes);
    if (!s) return ItemError;
    s->ndim = 1;
    s->is_c_contig = 1;
    s->is_f_contig = 1;
    s->offset = 0;
    s->base = (void*)base;
    array_num_shape_dims(s)[0] = base->length;
    array_num_shape_strides(s)[0] = 1;
    view->extra = (int64_t)(uintptr_t)s;
    base->is_pinned = 1;
    return { .array_num = view };
}

// matmul(a, b) - matrix product. Supports 1-D·1-D (dot scalar), 2-D·2-D,
// 1-D·2-D and 2-D·1-D (vector-matrix). int·int → int64, else float.
Item fn_matmul(Item a_item, Item b_item) {
    GUARD_ERROR2(a_item, b_item);
    if (get_type_id(a_item) != LMD_TYPE_ARRAY_NUM || get_type_id(b_item) != LMD_TYPE_ARRAY_NUM) {
        log_error("fn_matmul: both operands must be typed numeric arrays");
        return ItemError;
    }
    ArrayNum* A = a_item.array_num;
    ArrayNum* B = b_item.array_num;
    int64_t a_shp[32], a_str[32], b_shp[32], b_str[32];
    int a_ndim = get_shape_strides(A, a_shp, a_str);
    int b_ndim = get_shape_strides(B, b_shp, b_str);
    bool rf = !elem_is_int(A->get_elem_type()) || !elem_is_int(B->get_elem_type());
    ArrayNumElemType ret_et = rf ? ELEM_FLOAT : ELEM_INT64;

    if (a_ndim == 1 && b_ndim == 1) {           // dot product → scalar
        if (a_shp[0] != b_shp[0]) { log_error("fn_matmul: length mismatch %lld vs %lld", (long long)a_shp[0], (long long)b_shp[0]); return ItemError; }
        double sum = 0;
        for (int64_t k = 0; k < a_shp[0]; k++)
            sum += read_arr_elem_as_double(A, k * a_str[0]) * read_arr_elem_as_double(B, k * b_str[0]);
        return rf ? push_d(sum) : push_l((int64_t)sum);
    }
    if (a_ndim == 2 && b_ndim == 2) {           // (m,k)·(k,n) → (m,n)
        int64_t m = a_shp[0], k = a_shp[1], n = b_shp[1];
        if (k != b_shp[0]) { log_error("fn_matmul: inner dim mismatch %lld vs %lld", (long long)k, (long long)b_shp[0]); return ItemError; }
        int64_t out_shape[2] = { m, n };
        ArrayNum* R = alloc_ndim_arraynum(ret_et, 2, out_shape);
        if (!R) return ItemError;
        for (int64_t i = 0; i < m; i++)
            for (int64_t j = 0; j < n; j++) {
                double sum = 0;
                for (int64_t p = 0; p < k; p++)
                    sum += read_arr_elem_as_double(A, i * a_str[0] + p * a_str[1])
                         * read_arr_elem_as_double(B, p * b_str[0] + j * b_str[1]);
                if (rf) R->float_items[i * n + j] = sum; else R->items[i * n + j] = (int64_t)sum;
            }
        return { .array_num = R };
    }
    if (a_ndim == 1 && b_ndim == 2) {           // (k)·(k,n) → (n)
        int64_t k = a_shp[0], n = b_shp[1];
        if (k != b_shp[0]) { log_error("fn_matmul: dim mismatch"); return ItemError; }
        ArrayNum* R = array_num_new(ret_et, n);
        if (!R) return ItemError;
        for (int64_t j = 0; j < n; j++) {
            double sum = 0;
            for (int64_t p = 0; p < k; p++)
                sum += read_arr_elem_as_double(A, p * a_str[0]) * read_arr_elem_as_double(B, p * b_str[0] + j * b_str[1]);
            if (rf) R->float_items[j] = sum; else R->items[j] = (int64_t)sum;
        }
        return { .array_num = R };
    }
    if (a_ndim == 2 && b_ndim == 1) {           // (m,k)·(k) → (m)
        int64_t m = a_shp[0], k = a_shp[1];
        if (k != b_shp[0]) { log_error("fn_matmul: dim mismatch"); return ItemError; }
        ArrayNum* R = array_num_new(ret_et, m);
        if (!R) return ItemError;
        for (int64_t i = 0; i < m; i++) {
            double sum = 0;
            for (int64_t p = 0; p < k; p++)
                sum += read_arr_elem_as_double(A, i * a_str[0] + p * a_str[1]) * read_arr_elem_as_double(B, p * b_str[0]);
            if (rf) R->float_items[i] = sum; else R->items[i] = (int64_t)sum;
        }
        return { .array_num = R };
    }
    log_error("fn_matmul: only 1-D and 2-D operands supported (got %d-D and %d-D)", a_ndim, b_ndim);
    return ItemError;
}

// helper: pick the result elem_type for a binary join (concat/stack)
static ArrayNumElemType join_result_etype(ArrayNum* a, ArrayNum* b) {
    if (!elem_is_int(a->get_elem_type()) || !elem_is_int(b->get_elem_type())) return ELEM_FLOAT;
    if (a->get_elem_type() == b->get_elem_type()) return a->get_elem_type();
    return ELEM_INT64;
}

// concat(a, b) - join two arrays along axis 0. Trailing dims must match.
Item fn_concat(Item a_item, Item b_item) {
    GUARD_ERROR2(a_item, b_item);
    if (get_type_id(a_item) != LMD_TYPE_ARRAY_NUM || get_type_id(b_item) != LMD_TYPE_ARRAY_NUM) {
        log_error("fn_concat: both operands must be typed numeric arrays");
        return ItemError;
    }
    ArrayNum* A = a_item.array_num;
    ArrayNum* B = b_item.array_num;
    int64_t a_shp[32], a_str[32], b_shp[32], b_str[32];
    int a_ndim = get_shape_strides(A, a_shp, a_str);
    int b_ndim = get_shape_strides(B, b_shp, b_str);
    if (a_ndim != b_ndim) { log_error("fn_concat: ndim mismatch (%d vs %d)", a_ndim, b_ndim); return ItemError; }
    for (int ax = 1; ax < a_ndim; ax++)
        if (a_shp[ax] != b_shp[ax]) { log_error("fn_concat: trailing dim %d mismatch (%lld vs %lld)", ax, (long long)a_shp[ax], (long long)b_shp[ax]); return ItemError; }

    ArrayNumElemType ret_et = join_result_etype(A, B);
    int64_t out_shape[32];
    out_shape[0] = a_shp[0] + b_shp[0];
    for (int ax = 1; ax < a_ndim; ax++) out_shape[ax] = a_shp[ax];
    ArrayNum* R = (a_ndim >= 2) ? alloc_ndim_arraynum(ret_et, a_ndim, out_shape)
                                : array_num_new(ret_et, out_shape[0]);
    if (!R) return ItemError;
    arr_num_copy_into(A, R, 0);
    arr_num_copy_into(B, R, A->length);
    return { .array_num = R };
}

// stack(a, b) - stack two equally-shaped arrays along a new leading axis.
Item fn_stack(Item a_item, Item b_item) {
    GUARD_ERROR2(a_item, b_item);
    if (get_type_id(a_item) != LMD_TYPE_ARRAY_NUM || get_type_id(b_item) != LMD_TYPE_ARRAY_NUM) {
        log_error("fn_stack: both operands must be typed numeric arrays");
        return ItemError;
    }
    ArrayNum* A = a_item.array_num;
    ArrayNum* B = b_item.array_num;
    int64_t a_shp[32], a_str[32], b_shp[32], b_str[32];
    int a_ndim = get_shape_strides(A, a_shp, a_str);
    int b_ndim = get_shape_strides(B, b_shp, b_str);
    if (a_ndim != b_ndim) { log_error("fn_stack: ndim mismatch (%d vs %d)", a_ndim, b_ndim); return ItemError; }
    for (int ax = 0; ax < a_ndim; ax++)
        if (a_shp[ax] != b_shp[ax]) { log_error("fn_stack: shape mismatch at axis %d (%lld vs %lld)", ax, (long long)a_shp[ax], (long long)b_shp[ax]); return ItemError; }
    if (a_ndim + 1 > 32) { log_error("fn_stack: result ndim exceeds 32"); return ItemError; }

    ArrayNumElemType ret_et = join_result_etype(A, B);
    int64_t out_shape[32];
    out_shape[0] = 2;
    for (int ax = 0; ax < a_ndim; ax++) out_shape[ax + 1] = a_shp[ax];
    ArrayNum* R = alloc_ndim_arraynum(ret_et, a_ndim + 1, out_shape);
    if (!R) return ItemError;
    arr_num_copy_into(A, R, 0);
    arr_num_copy_into(B, R, A->length);
    return { .array_num = R };
}

// Extract the slice src[..., axis in [lo, hi), ...] into a new owned C-contiguous
// ArrayNum.  Walks the output shape in C-order, mapping each position to the
// source flat offset (handles non-contiguous/strided/transposed sources).
static ArrayNum* arr_num_slice_axis(ArrayNum* src, int ndim, int64_t* shp, int64_t* str,
                                    int axis, int64_t lo, int64_t hi) {
    int64_t out_shape[32];
    for (int d = 0; d < ndim; d++) out_shape[d] = shp[d];
    out_shape[axis] = hi - lo;
    int64_t total = 1;
    for (int d = 0; d < ndim; d++) total *= out_shape[d];

    ArrayNumElemType et = src->get_elem_type();
    ArrayNum* result = (ndim >= 2) ? alloc_ndim_arraynum(et, ndim, out_shape)
                                   : array_num_new(et, out_shape[0]);
    if (!result || total == 0) return result;

    int elem_size = ELEM_TYPE_SIZE[et >> 4];
    int64_t idx[32] = {0};
    int64_t src_off = lo * str[axis];   // bake in the axis offset of the chunk
    for (int64_t i = 0; i < total; i++) {
        memcpy((char*)result->data + (size_t)i * elem_size,
               (char*)src->data + (size_t)src_off * elem_size, elem_size);
        for (int d = ndim - 1; d >= 0; d--) {
            idx[d]++; src_off += str[d];
            if (idx[d] < out_shape[d]) break;
            idx[d] = 0; src_off -= out_shape[d] * str[d];
        }
    }
    return result;
}

// split(arr, n, axis) - divide a typed array into n equal sub-arrays along
// `axis` (axis length must be divisible by n).  Returns a list of n sub-arrays.
Item fn_array_split(Item arr_item, int64_t n, int64_t axis) {
    GUARD_ERROR1(arr_item);
    if (get_type_id(arr_item) != LMD_TYPE_ARRAY_NUM) {
        log_error("split: expected typed numeric array");
        return ItemError;
    }
    ArrayNum* arr = arr_item.array_num;
    if (!arr) return ItemError;
    if (n <= 0) { log_error("split: section count must be positive, got %lld", (long long)n); return ItemError; }

    int64_t shp[32], str[32];
    int ndim = get_shape_strides(arr, shp, str);
    if (axis < 0) axis += ndim;
    if (axis < 0 || axis >= ndim) {
        log_error("split: axis %lld out of range for %d-D array", (long long)axis, ndim);
        return ItemError;
    }
    if (shp[axis] % n != 0) {
        log_error("split: axis %lld length %lld not divisible by %lld sections",
                  (long long)axis, (long long)shp[axis], (long long)n);
        return ItemError;
    }
    int64_t chunk = shp[axis] / n;

    List* result = list();
    for (int64_t c = 0; c < n; c++) {
        ArrayNum* part = arr_num_slice_axis(arr, ndim, shp, str, (int)axis, c * chunk, (c + 1) * chunk);
        if (!part) return ItemError;
        list_push(result, (Item){ .array_num = part });
    }
    return { .array = result };
}

// ============================================================================
// Axis-aware reductions: sum/avg/prod (collapse) and cumsum/cumprod (running).
// One shared strided "lane" walker drives them all — for each output position
// (all coords except `axis`), the elements along `axis` form a lane.
// ============================================================================
enum ReduceOp { RED_SUM, RED_PROD, RED_MIN, RED_MAX, RED_AVG };

// Contiguous (stride==1) typed reduction over a native element type.  Reads
// elements directly (no per-element type switch) so the loop vectorizes.  The
// accumulation order and double conversion are identical to the strided walker
// (first element seeds the accumulator, folded in element order, in double), so
// results are bit-identical.  min/max and integer folds are associative and
// vectorize; the float sum/prod left-fold stays scalar (no reassociation).
template <typename T, int OP>
static double k_reduce_contig(const T* __restrict a, int64_t n) {
    double acc = (double)a[0];
    if constexpr (OP == RED_MIN || OP == RED_MAX) {
        LMD_VEC_LOOP
        for (int64_t i = 1; i < n; i++) {
            double v = (double)a[i];
            if constexpr (OP == RED_MIN) { if (v < acc) acc = v; }
            else                         { if (v > acc) acc = v; }
        }
    } else {  // SUM / AVG / PROD — strict left-fold in double
        for (int64_t i = 1; i < n; i++) {
            double v = (double)a[i];
            if constexpr (OP == RED_PROD) acc *= v;
            else                          acc += v;
        }
        if constexpr (OP == RED_AVG) acc /= (double)n;
    }
    return acc;
}

// element types the contiguous reducer covers (FLOAT16/BOOL fall back to strided).
static inline bool is_contig_reducible(ArrayNumElemType et) {
    return et == ELEM_INT || et == ELEM_INT64 || et == ELEM_FLOAT || et == ELEM_FLOAT64 ||
           et == ELEM_FLOAT32 || et == ELEM_INT8 || et == ELEM_INT16 || et == ELEM_INT32 ||
           et == ELEM_UINT8 || et == ELEM_UINT16 || et == ELEM_UINT32 || et == ELEM_UINT64;
}

// Dispatch a contiguous reduction over `len` elements starting at element index
// `base_off` of arr->data, selecting the kernel by element type and op.  Caller
// must ensure the element type is contig-reducible.
static double reduce_contig_dispatch(ArrayNum* arr, int64_t base_off, int64_t len, int op) {
    #define LMD_RED(CT) do { const CT* p = (const CT*)arr->data + base_off; \
        switch (op) { \
            case RED_SUM:  return k_reduce_contig<CT, RED_SUM>(p, len); \
            case RED_PROD: return k_reduce_contig<CT, RED_PROD>(p, len); \
            case RED_MIN:  return k_reduce_contig<CT, RED_MIN>(p, len); \
            case RED_MAX:  return k_reduce_contig<CT, RED_MAX>(p, len); \
            case RED_AVG:  return k_reduce_contig<CT, RED_AVG>(p, len); \
        } } while (0)
    switch (arr->get_elem_type()) {
        case ELEM_INT: case ELEM_INT64:     LMD_RED(int64_t);  break;
        case ELEM_FLOAT: case ELEM_FLOAT64: LMD_RED(double);   break;
        case ELEM_FLOAT32:                  LMD_RED(float);    break;
        case ELEM_INT8:                     LMD_RED(int8_t);   break;
        case ELEM_INT16:                    LMD_RED(int16_t);  break;
        case ELEM_INT32:                    LMD_RED(int32_t);  break;
        case ELEM_UINT8:                    LMD_RED(uint8_t);  break;
        case ELEM_UINT8_CLAMPED:            LMD_RED(uint8_t);  break;
        case ELEM_UINT16:                   LMD_RED(uint16_t); break;
        case ELEM_UINT32:                   LMD_RED(uint32_t); break;
        case ELEM_UINT64:                   LMD_RED(uint64_t); break;
        default: break;
    }
    #undef LMD_RED
    return (op == RED_PROD) ? 1.0 : 0.0;  // unreachable for contig-reducible types
}

// Reduce one lane: elements at base_off, base_off+stride, … (len of them).
static double reduce_lane(ArrayNum* arr, int64_t base_off, int64_t stride, int64_t len, int op) {
    if (len <= 0) return (op == RED_PROD) ? 1.0 : 0.0;
    // contiguous fast path: a unit-stride lane over a native type vectorizes.
    if (stride == 1 && is_contig_reducible(arr->get_elem_type())) {
        return reduce_contig_dispatch(arr, base_off, len, op);
    }
    double acc = read_arr_elem_as_double(arr, base_off);
    for (int64_t k = 1; k < len; k++) {
        double v = read_arr_elem_as_double(arr, base_off + k * stride);
        switch (op) {
            case RED_SUM: case RED_AVG: acc += v; break;
            case RED_PROD:              acc *= v; break;
            case RED_MIN: if (v < acc)  acc = v;  break;
            case RED_MAX: if (v > acc)  acc = v;  break;
        }
    }
    if (op == RED_AVG) acc /= (double)len;
    return acc;
}

// Whole-array reduction → double accumulator, correct for every element type.
// Owned arrays (contiguous in storage) use the vectorized contiguous kernel;
// strided views (and FLOAT16/BOOL) fall back to a correct n-d strided walk.
double array_num_reduce_double(ArrayNum* arr, int op) {
    if (!arr) return (op == RED_PROD) ? 1.0 : 0.0;
    int64_t n = arr->length;
    if (n <= 0) return (op == RED_PROD) ? 1.0 : 0.0;
    ArrayNumElemType et = arr->get_elem_type();
    // owned arrays are contiguous in their storage buffer (1-D or row-major n-d).
    if (!arr->is_view && is_contig_reducible(et)) {
        return reduce_contig_dispatch(arr, 0, n, op);
    }
    // strided / view / FLOAT16 / BOOL: walk every element in row-major logical
    // order via the shape side-table, reading each by its true type.
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(arr, shp, str);
    int64_t idx[32] = {0};
    int64_t off = 0;
    double acc = read_arr_elem_as_double(arr, 0);
    for (int64_t cnt = 1; cnt < n; cnt++) {
        for (int d = ndim - 1; d >= 0; d--) {
            idx[d]++; off += str[d];
            if (idx[d] < shp[d]) break;
            idx[d] = 0; off -= shp[d] * str[d];
        }
        double v = read_arr_elem_as_double(arr, off);
        switch (op) {
            case RED_SUM: case RED_AVG: acc += v; break;
            case RED_PROD:              acc *= v; break;
            case RED_MIN: if (v < acc)  acc = v;  break;
            case RED_MAX: if (v > acc)  acc = v;  break;
        }
    }
    if (op == RED_AVG) acc /= (double)n;
    return acc;
}

// Read the axis argument as an int64 (returns INT64_MIN sentinel on bad type).
static int64_t parse_axis(Item axis_item) {
    TypeId t = get_type_id(axis_item);
    if (t == LMD_TYPE_INT)   return axis_item.get_int56();
    if (t == LMD_TYPE_INT64) return axis_item.get_int64();
    return INT64_MIN;
}

// Collapse `axis` of a typed array via the reduction op.  Result is an
// (ndim-1)-D ArrayNum, or a scalar Item when the input is 1-D.
static Item array_num_reduce_axis(Item arr_item, Item axis_item, int op, const char* name) {
    GUARD_ERROR1(arr_item);
    if (get_type_id(arr_item) != LMD_TYPE_ARRAY_NUM) {
        log_error("%s(arr, axis): axis reduction requires a typed numeric array", name);
        return ItemError;
    }
    int64_t axis = parse_axis(axis_item);
    if (axis == INT64_MIN) { log_error("%s: axis must be an integer", name); return ItemError; }
    ArrayNum* arr = arr_item.array_num;
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(arr, shp, str);
    if (axis < 0) axis += ndim;
    if (axis < 0 || axis >= ndim) {
        log_error("%s: axis %lld out of range for %d-D array", name, (long long)axis, ndim);
        return ItemError;
    }
    int64_t axis_len = shp[axis], axis_str = str[axis];
    bool result_float = !elem_is_int(arr->get_elem_type()) || op == RED_AVG;

    // 1-D input → scalar
    if (ndim == 1) {
        double acc = reduce_lane(arr, 0, axis_str, axis_len, op);
        return result_float ? push_d(acc) : push_l((int64_t)acc);
    }

    // build output shape and the non-axis dims/strides (same order)
    int64_t out_shape[32], na_shape[32], na_str[32];
    int out_ndim = 0;
    for (int d = 0; d < ndim; d++) {
        if (d == axis) continue;
        out_shape[out_ndim] = shp[d];
        na_shape[out_ndim]  = shp[d];
        na_str[out_ndim]    = str[d];
        out_ndim++;
    }
    int64_t out_total = 1;
    for (int d = 0; d < out_ndim; d++) out_total *= out_shape[d];

    ArrayNumElemType ret_et = result_float ? ELEM_FLOAT : ELEM_INT64;
    ArrayNum* result = (out_ndim >= 2) ? alloc_ndim_arraynum(ret_et, out_ndim, out_shape)
                                       : array_num_new(ret_et, out_shape[0]);
    if (!result) return ItemError;

    int64_t idx[32] = {0};
    int64_t base_off = 0;
    for (int64_t o = 0; o < out_total; o++) {
        double acc = reduce_lane(arr, base_off, axis_str, axis_len, op);
        if (result_float) result->float_items[o] = acc;
        else              result->items[o] = (int64_t)acc;
        for (int d = out_ndim - 1; d >= 0; d--) {
            idx[d]++; base_off += na_str[d];
            if (idx[d] < na_shape[d]) break;
            idx[d] = 0; base_off -= na_shape[d] * na_str[d];
        }
    }
    return { .array_num = result };
}

// Running scan along `axis` (cumsum/cumprod) — same-shape owned result.
static Item array_num_cumulative_axis(Item arr_item, Item axis_item, bool is_prod, const char* name) {
    GUARD_ERROR1(arr_item);
    if (get_type_id(arr_item) != LMD_TYPE_ARRAY_NUM) {
        log_error("%s(arr, axis): requires a typed numeric array", name);
        return ItemError;
    }
    int64_t axis = parse_axis(axis_item);
    if (axis == INT64_MIN) { log_error("%s: axis must be an integer", name); return ItemError; }
    ArrayNum* arr = arr_item.array_num;
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(arr, shp, str);
    if (axis < 0) axis += ndim;
    if (axis < 0 || axis >= ndim) {
        log_error("%s: axis %lld out of range for %d-D array", name, (long long)axis, ndim);
        return ItemError;
    }
    bool result_float = !elem_is_int(arr->get_elem_type());
    ArrayNumElemType ret_et = result_float ? ELEM_FLOAT : ELEM_INT64;

    // result: same shape, owned C-contiguous
    int64_t full_shape[32];
    for (int d = 0; d < ndim; d++) full_shape[d] = shp[d];
    ArrayNum* result = (ndim >= 2) ? alloc_ndim_arraynum(ret_et, ndim, full_shape)
                                   : array_num_new(ret_et, shp[0]);
    if (!result) return ItemError;

    // result C-order strides
    int64_t res_str[32];
    { int64_t s = 1; for (int d = ndim - 1; d >= 0; d--) { res_str[d] = s; s *= shp[d]; } }

    int64_t axis_len = shp[axis], src_axis_str = str[axis], res_axis_str = res_str[axis];

    // non-axis dims + their source / result strides
    int64_t na_shape[32], na_src[32], na_res[32];
    int na_ndim = 0;
    for (int d = 0; d < ndim; d++) {
        if (d == axis) continue;
        na_shape[na_ndim] = shp[d];
        na_src[na_ndim]   = str[d];
        na_res[na_ndim]   = res_str[d];
        na_ndim++;
    }
    int64_t na_total = 1;
    for (int d = 0; d < na_ndim; d++) na_total *= na_shape[d];

    int64_t idx[32] = {0};
    int64_t src_base = 0, res_base = 0;
    for (int64_t lane = 0; lane < na_total; lane++) {
        double running = is_prod ? 1.0 : 0.0;
        for (int64_t a = 0; a < axis_len; a++) {
            double v = read_arr_elem_as_double(arr, src_base + a * src_axis_str);
            running = is_prod ? running * v : running + v;
            int64_t ro = res_base + a * res_axis_str;
            if (result_float) result->float_items[ro] = running;
            else              result->items[ro] = (int64_t)running;
        }
        for (int d = na_ndim - 1; d >= 0; d--) {
            idx[d]++; src_base += na_src[d]; res_base += na_res[d];
            if (idx[d] < na_shape[d]) break;
            idx[d] = 0; src_base -= na_shape[d] * na_src[d]; res_base -= na_shape[d] * na_res[d];
        }
    }
    return { .array_num = result };
}

Item fn_min_axis(Item arr, Item axis)     { return array_num_reduce_axis(arr, axis, RED_MIN,  "min");  }
Item fn_max_axis(Item arr, Item axis)     { return array_num_reduce_axis(arr, axis, RED_MAX,  "max");  }

// ============================================================================
// Boolean mask indexing:  arr[mask]  where mask is an ELEM_BOOL ArrayNum.
//   • full mask (mask.shape == arr.shape): flatten-select → 1-D of kept elements
//   • 1-D mask of length arr.shape[0]:    leading-axis select → (k, *trailing)
// Result keeps arr's element type; raw bytes are copied (exact, no float round-trip).
// Strided sources (views/transpose) handled.
// ============================================================================
Item fn_mask_index(Item arr_item, Item mask_item) {
    GUARD_ERROR2(arr_item, mask_item);
    if (get_type_id(arr_item) != LMD_TYPE_ARRAY_NUM ||
        get_type_id(mask_item) != LMD_TYPE_ARRAY_NUM) {
        log_error("arr[mask]: both array and mask must be typed numeric arrays");
        return ItemError;
    }
    ArrayNum* arr = arr_item.array_num;
    ArrayNum* mask = mask_item.array_num;
    if (mask->get_elem_type() != ELEM_BOOL) {
        log_error("arr[mask]: the index array must be boolean (got non-bool elements)");
        return ItemError;
    }
    int64_t a_shape[32], a_str[32], m_shape[32], m_str[32];
    int a_ndim = get_shape_strides(arr, a_shape, a_str);
    int m_ndim = get_shape_strides(mask, m_shape, m_str);
    ArrayNumElemType et = arr->get_elem_type();
    size_t esz = ELEM_TYPE_SIZE[et >> 4];

    // ---- full mask: identical shape → flatten-select ----
    bool same_shape = (m_ndim == a_ndim);
    for (int d = 0; same_shape && d < a_ndim; d++)
        if (m_shape[d] != a_shape[d]) same_shape = false;
    if (same_shape) {
        int64_t total = 1;
        for (int d = 0; d < a_ndim; d++) total *= a_shape[d];
        // pass 1: count selected
        int64_t k = 0;
        { int64_t idx[32] = {0}, mk_off = 0;
          for (int64_t o = 0; o < total; o++) {
            if (read_arr_elem_as_double(mask, mk_off) != 0.0) k++;
            for (int d = a_ndim - 1; d >= 0; d--) {
                idx[d]++; mk_off += m_str[d];
                if (idx[d] < a_shape[d]) break;
                idx[d] = 0; mk_off -= m_shape[d] * m_str[d];
            }
          } }
        ArrayNum* result = array_num_new(et, k);
        if (!result) return ItemError;
        // pass 2: copy kept elements
        char* dbase = (char*)result->data;
        char* sbase = (char*)arr->data;
        { int64_t idx[32] = {0}, a_off = 0, mk_off = 0, w = 0;
          for (int64_t o = 0; o < total; o++) {
            if (read_arr_elem_as_double(mask, mk_off) != 0.0) {
                memcpy(dbase + (size_t)w * esz, sbase + (size_t)a_off * esz, esz); w++;
            }
            for (int d = a_ndim - 1; d >= 0; d--) {
                idx[d]++; a_off += a_str[d]; mk_off += m_str[d];
                if (idx[d] < a_shape[d]) break;
                idx[d] = 0; a_off -= a_shape[d] * a_str[d]; mk_off -= m_shape[d] * m_str[d];
            }
          } }
        return { .array_num = result };
    }

    // ---- 1-D mask along the leading axis ----
    if (m_ndim == 1 && m_shape[0] == a_shape[0]) {
        int64_t k = 0;
        for (int64_t i = 0; i < a_shape[0]; i++)
            if (read_arr_elem_as_double(mask, i * m_str[0]) != 0.0) k++;

        if (a_ndim == 1) {
            ArrayNum* result = array_num_new(et, k);
            if (!result) return ItemError;
            char* dbase = (char*)result->data;
            char* sbase = (char*)arr->data;
            int64_t w = 0;
            for (int64_t i = 0; i < a_shape[0]; i++)
                if (read_arr_elem_as_double(mask, i * m_str[0]) != 0.0)
                    memcpy(dbase + (size_t)(w++) * esz, sbase + (size_t)(i * a_str[0]) * esz, esz);
            return { .array_num = result };
        }
        // N-D: result shape (k, arr.shape[1..]); copy each kept leading-axis slab
        int64_t out_shape[32]; out_shape[0] = k;
        int64_t slab = 1;
        for (int d = 1; d < a_ndim; d++) { out_shape[d] = a_shape[d]; slab *= a_shape[d]; }
        ArrayNum* result = alloc_ndim_arraynum(et, a_ndim, out_shape);
        if (!result) return ItemError;
        char* dbase = (char*)result->data;
        char* sbase = (char*)arr->data;
        int64_t w = 0;
        for (int64_t i = 0; i < a_shape[0]; i++) {
            if (read_arr_elem_as_double(mask, i * m_str[0]) == 0.0) continue;
            int64_t tidx[32] = {0}, s_off = i * a_str[0];
            for (int64_t e = 0; e < slab; e++) {
                memcpy(dbase + (size_t)(w * slab + e) * esz, sbase + (size_t)s_off * esz, esz);
                for (int d = a_ndim - 1; d >= 1; d--) {
                    tidx[d]++; s_off += a_str[d];
                    if (tidx[d] < a_shape[d]) break;
                    tidx[d] = 0; s_off -= a_shape[d] * a_str[d];
                }
            }
            w++;
        }
        return { .array_num = result };
    }

    log_error("arr[mask]: mask shape is incompatible with the array");
    return ItemError;
}

// arr[mask] = scalar | values — masked in-place write, the write-counterpart to
// fn_mask_index.  `mask` is a bool ArrayNum the same shape as `arr`.  Reached only
// from procedural index-assign statements (so it is procedural by construction).
// Scalar RHS fills every selected element; array RHS is consumed in order.
void fn_index_assign(Item arr_item, Item idx_item, Item val_item) {
    // Generic Array with a plain integer index: the index was statically typed ANY
    // (e.g. a range-for loop variable `for i in a to b { arr[i] = v }`) but is
    // dynamically an int. Route to the ordinary element write — masks/ranges only
    // apply to typed numeric arrays. fn_array_set handles negatives and bounds.
    {
        TypeId arr_tid = get_type_id(arr_item);
        TypeId idx_tid = get_type_id(idx_item);
        if (arr_tid == LMD_TYPE_ARRAY &&
            (idx_tid == LMD_TYPE_INT || idx_tid == LMD_TYPE_INT64)) {
            int64_t i = (idx_tid == LMD_TYPE_INT) ? idx_item.get_int56() : idx_item.get_int64();
            fn_array_set(arr_item.array, i, val_item);
            return;
        }
    }
    if (get_type_id(arr_item) != LMD_TYPE_ARRAY_NUM) {
        log_error("arr[idx] = v: masked assignment requires a typed numeric array target");
        return;
    }
    ArrayNum* arr = arr_item.array_num;
    if (arr->is_view && !arr->is_mutable_view) {
        log_error("arr[mask] = v: cannot mutate a read-only view; copy() first");
        return;
    }
    TypeId it = get_type_id(idx_item);
    // plain integer index — element write (this path is reached when the index is
    // dynamically typed (ANY), e.g. an index variable, that turns out to be an int).
    if (it == LMD_TYPE_INT || it == LMD_TYPE_INT64) {
        int64_t i = (it == LMD_TYPE_INT) ? idx_item.get_int56() : idx_item.get_int64();
        if (i < 0) i += arr->length;
        if (i >= 0 && i < arr->length) array_num_set_item(arr, i, val_item);
        return;
    }
    if (it == LMD_TYPE_RANGE) {
        // range slicing is not an indexing read either (use subview); keep the
        // write side consistent rather than inventing write-only slice semantics.
        log_error("arr[a to b] = v: range slice-assignment is not supported; use subview/element writes");
        return;
    }
    if (it != LMD_TYPE_ARRAY_NUM || idx_item.array_num->get_elem_type() != ELEM_BOOL) {
        log_error("arr[idx] = v: index must be a boolean mask (got %s)", get_type_name(it));
        return;
    }
    ArrayNum* mask = idx_item.array_num;
    int64_t a_shape[32], a_str[32], m_shape[32], m_str[32];
    int a_ndim = get_shape_strides(arr, a_shape, a_str);
    int m_ndim = get_shape_strides(mask, m_shape, m_str);
    bool same = (m_ndim == a_ndim);
    for (int d = 0; same && d < a_ndim; d++) if (m_shape[d] != a_shape[d]) same = false;
    if (!same) {
        log_error("arr[mask] = v: mask shape must match the array shape");
        return;
    }
    int64_t total = 1;
    for (int d = 0; d < a_ndim; d++) total *= a_shape[d];
    bool block = (get_type_id(val_item) == LMD_TYPE_ARRAY_NUM);
    ArrayNum* vals = block ? val_item.array_num : nullptr;
    int64_t idx[32] = {0}, a_off = 0, m_off = 0, w = 0;
    for (int64_t o = 0; o < total; o++) {
        if (read_arr_elem_as_double(mask, m_off) != 0.0) {
            Item v;
            if (block) {
                if (w >= vals->length) break;  // array RHS exhausted
                v = vector_get({ .array_num = vals }, w++);
            } else { v = val_item; }
            array_num_set_item(arr, a_off, v);
        }
        for (int d = a_ndim - 1; d >= 0; d--) {
            idx[d]++; a_off += a_str[d]; m_off += m_str[d];
            if (idx[d] < a_shape[d]) break;
            idx[d] = 0; a_off -= a_shape[d] * a_str[d]; m_off -= m_shape[d] * m_str[d];
        }
    }
}

Item fn_sum_axis(Item arr, Item axis)     { return array_num_reduce_axis(arr, axis, RED_SUM,  "sum");  }
Item fn_avg_axis(Item arr, Item axis)     { return array_num_reduce_axis(arr, axis, RED_AVG,  "avg");  }
Item fn_prod_axis(Item arr, Item axis)    { return array_num_reduce_axis(arr, axis, RED_PROD, "prod"); }
Item fn_cumsum_axis(Item arr, Item axis)  { return array_num_cumulative_axis(arr, axis, false, "cumsum");  }
Item fn_cumprod_axis(Item arr, Item axis) { return array_num_cumulative_axis(arr, axis, true,  "cumprod"); }

//==============================================================================
// Image stencil engine — windowed neighbourhood ops over ArrayNum
//
// Slide a Kh×Kw window (centred on each output's input position) over the spatial
// dims of `in` (2-D H×W, or 3-D H×W×C applied per-channel) and reduce at each
// output position.  Border positions outside `in` are filled per `border`.
// Output is ELEM_FLOAT.  Convolution (blur/sharpen/sobel), morphology
// (erode/dilate), rank filters (median) and pooling all reduce to this primitive.
//==============================================================================

enum StencilOp  { STENCIL_DOT = 0, STENCIL_MIN = 1, STENCIL_MAX = 2, STENCIL_MEDIAN = 3, STENCIL_MEAN = 4 };
enum BorderMode { BORDER_CONSTANT = 0, BORDER_EDGE = 1, BORDER_REFLECT = 2, BORDER_WRAP = 3 };

// Resolve an out-of-bounds index `i` into [0,n) per the border mode; returns -1 to
// signal "use the constant border value" (BORDER_CONSTANT, or degenerate n<=0).
static inline int64_t stencil_border_index(int64_t i, int64_t n, int border) {
    if (n <= 0) return -1;
    if (i >= 0 && i < n) return i;
    switch (border) {
        case BORDER_EDGE:    return (i < 0) ? 0 : n - 1;
        case BORDER_REFLECT: {                          // reflect w/o repeating the edge
            if (n == 1) return 0;
            int64_t p = 2 * (n - 1);
            int64_t m = ((i % p) + p) % p;
            return (m < n) ? m : p - m;
        }
        case BORDER_WRAP:    return ((i % n) + n) % n;
        default:             return -1;                 // BORDER_CONSTANT
    }
}

// Sample in[ii, jj, c] with border handling; returns `bval` when off-image under
// BORDER_CONSTANT.
static inline double stencil_sample(ArrayNum* in, int64_t ii, int64_t jj, int64_t c,
                                    int64_t H, int64_t W, int ndim, const int64_t* str,
                                    int border, double bval) {
    int64_t ri = stencil_border_index(ii, H, border);
    int64_t rj = stencil_border_index(jj, W, border);
    if (ri < 0 || rj < 0) return bval;
    int64_t off = ri * str[0] + rj * str[1] + ((ndim == 3) ? c * str[2] : 0);
    return read_arr_elem_as_double(in, off);
}

#define STENCIL_MEDIAN_CAP 4096

Item array_num_stencil(Item in_item, Item kernel_item, int op, int border,
                       double border_value, int64_t stride_h, int64_t stride_w,
                       int64_t pad_h, int64_t pad_w) {
    GUARD_ERROR2(in_item, kernel_item);
    if (get_type_id(in_item) != LMD_TYPE_ARRAY_NUM || get_type_id(kernel_item) != LMD_TYPE_ARRAY_NUM) {
        log_error("stencil: input and kernel must be typed numeric arrays");
        return ItemError;
    }
    ArrayNum* in = in_item.array_num;
    ArrayNum* ker = kernel_item.array_num;
    int64_t ishp[32], istr[32], kshp[32], kstr[32];
    int indim = get_shape_strides(in, ishp, istr);
    int kndim = get_shape_strides(ker, kshp, kstr);
    if ((indim != 2 && indim != 3) || kndim != 2) {
        log_error("stencil: input must be 2-D (H,W) or 3-D (H,W,C); kernel 2-D (Kh,Kw)");
        return ItemError;
    }
    if (stride_h < 1) stride_h = 1;
    if (stride_w < 1) stride_w = 1;
    int64_t H = ishp[0], W = ishp[1], C = (indim == 3) ? ishp[2] : 1;
    int64_t Kh = kshp[0], Kw = kshp[1];
    int64_t ph = (pad_h < 0) ? Kh / 2 : pad_h;   // centred (Kh/2) vs top-left (0)
    int64_t pw = (pad_w < 0) ? Kw / 2 : pad_w;
    if (Kh * Kw > STENCIL_MEDIAN_CAP && op == STENCIL_MEDIAN) {
        log_error("stencil: median kernel too large (%lld > %d)", (long long)(Kh * Kw), STENCIL_MEDIAN_CAP);
        return ItemError;
    }
    int64_t oH = (H + stride_h - 1) / stride_h, oW = (W + stride_w - 1) / stride_w;
    int64_t oshape[3]; oshape[0] = oH; oshape[1] = oW; if (indim == 3) oshape[2] = C;
    ArrayNum* out = alloc_ndim_arraynum(ELEM_FLOAT, indim, oshape);
    if (!out) return ItemError;

    double* dst = out->float_items;          // contiguous, row-major (…, oj, c)
    int64_t w = 0;
    double medbuf[STENCIL_MEDIAN_CAP];
    for (int64_t oi = 0; oi < oH; oi++) {
        int64_t ci = oi * stride_h;
        for (int64_t oj = 0; oj < oW; oj++) {
            int64_t cj = oj * stride_w;
            for (int64_t c = 0; c < C; c++) {
                double acc = 0.0, mn = 0.0, mx = 0.0, sum = 0.0;
                int64_t cnt = 0, mlen = 0;
                for (int64_t ki = 0; ki < Kh; ki++) {
                    int64_t ii = ci + ki - ph;
                    for (int64_t kj = 0; kj < Kw; kj++) {
                        double kval = read_arr_elem_as_double(ker, ki * kstr[0] + kj * kstr[1]);
                        double sval = stencil_sample(in, ii, cj + kj - pw, c, H, W, indim, istr, border, border_value);
                        if (op == STENCIL_DOT) { acc += kval * sval; continue; }
                        if (kval == 0.0) continue;       // structuring element: 0 = not part of window
                        if (cnt == 0) { mn = mx = sval; } else { if (sval < mn) mn = sval; if (sval > mx) mx = sval; }
                        sum += sval; cnt++;
                        if (op == STENCIL_MEDIAN && mlen < STENCIL_MEDIAN_CAP) medbuf[mlen++] = sval;
                    }
                }
                double res;
                switch (op) {
                    case STENCIL_DOT:    res = acc; break;
                    case STENCIL_MIN:    res = (cnt > 0) ? mn : 0.0; break;
                    case STENCIL_MAX:    res = (cnt > 0) ? mx : 0.0; break;
                    case STENCIL_MEAN:   res = (cnt > 0) ? sum / (double)cnt : 0.0; break;
                    case STENCIL_MEDIAN:
                        if (mlen == 0) { res = 0.0; break; }
                        insertion_sort(medbuf, (size_t)mlen, sizeof(double), cmp_double_asc, NULL);
                        res = (mlen & 1) ? medbuf[mlen / 2]
                                         : (medbuf[mlen / 2 - 1] + medbuf[mlen / 2]) / 2.0;
                        break;
                    default:             res = 0.0; break;
                }
                dst[w++] = res;
            }
        }
    }
    return { .array_num = out };
}

// Build a ksize×ksize kernel of ones (box weights / full structuring element).
static ArrayNum* stencil_box_kernel(int64_t ksize) {
    if (ksize < 1) ksize = 1;
    int64_t shape[2] = { ksize, ksize };
    ArrayNum* k = alloc_ndim_arraynum(ELEM_FLOAT, 2, shape);
    if (k) for (int64_t i = 0; i < ksize * ksize; i++) k->float_items[i] = 1.0;
    return k;
}

static int64_t stencil_ksize_arg(Item ksize_item) {
    TypeId t = get_type_id(ksize_item);
    if (t == LMD_TYPE_INT)   return ksize_item.get_int56();
    if (t == LMD_TYPE_INT64) return ksize_item.get_int64();
    return 3;  // sensible default
}

// thin wrappers — each is a single stencil call (the whole toolkit stands on these).
Item fn_convolve(Item img, Item kernel) {
    return array_num_stencil(img, kernel, STENCIL_DOT, BORDER_EDGE, 0.0, 1, 1, -1, -1);  // centred
}
// op over a ksize×ksize box.  stride==1 → same-size centred filter; stride==ksize →
// non-overlapping top-left pooling.
static Item stencil_box_op(Item img, Item ksize_item, int op, int64_t stride, int64_t pad) {
    ArrayNum* ker = stencil_box_kernel(stencil_ksize_arg(ksize_item));
    if (!ker) return ItemError;
    return array_num_stencil(img, Item{ .array_num = ker }, op, BORDER_EDGE, 0.0, stride, stride, pad, pad);
}
Item fn_blur(Item img, Item ksize)          { return stencil_box_op(img, ksize, STENCIL_MEAN,   1, -1); }
Item fn_erode(Item img, Item ksize)         { return stencil_box_op(img, ksize, STENCIL_MIN,    1, -1); }
Item fn_dilate(Item img, Item ksize)        { return stencil_box_op(img, ksize, STENCIL_MAX,    1, -1); }
Item fn_median_filter(Item img, Item ksize) { return stencil_box_op(img, ksize, STENCIL_MEDIAN, 1, -1); }
// pooling: non-overlapping top-left windows (stride = ksize, pad = 0) downsample.
Item fn_maxpool(Item img, Item ksize)       { return stencil_box_op(img, ksize, STENCIL_MAX,  stencil_ksize_arg(ksize), 0); }
Item fn_avgpool(Item img, Item ksize)       { return stencil_box_op(img, ksize, STENCIL_MEAN, stencil_ksize_arg(ksize), 0); }

//==============================================================================
// Image I/O bridge (Typed Array 4, Scope 1.1)
//
// Convention (matches scikit-image img_as_float / img_as_ubyte):
//   - ubyte image: ELEM_UINT8, samples in [0, 255]
//   - float image: ELEM_FLOAT, samples in [0, 1]
// load() yields an (H, W, 4) RGBA ubyte image; the per-channel stencil ops and
// elementwise kernels then operate on it (work in float via as_float for filters
// that need fractional values, convert back with as_ubyte before save).
//==============================================================================

// Convert every element of `in` into a fresh contiguous ArrayNum of `out_etype`,
// computing v = read*scale, optionally rounded and clamped to [lo, hi].  Walks an
// N-D odometer so views (non-contiguous crops) convert correctly.  out_etype is
// ELEM_FLOAT or ELEM_UINT8 (the two image representations).
static Item array_num_convert(ArrayNum* in, ArrayNumElemType out_etype, double scale,
                              bool clamp_round, double lo, double hi) {
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(in, shp, str);
    if (ndim < 1) return ItemError;
    ArrayNum* out = alloc_ndim_arraynum(out_etype, ndim, shp);
    if (!out) return ItemError;
    int64_t total = 1;
    for (int d = 0; d < ndim; d++) total *= shp[d];
    double*  fout = (out_etype == ELEM_FLOAT) ? out->float_items : NULL;
    uint8_t* bout = (out_etype == ELEM_UINT8) ? (uint8_t*)out->data : NULL;
    int64_t idx[32]; for (int d = 0; d < ndim; d++) idx[d] = 0;
    for (int64_t lin = 0; lin < total; lin++) {
        int64_t off = 0;
        for (int d = 0; d < ndim; d++) off += idx[d] * str[d];
        double v = read_arr_elem_as_double(in, off) * scale;
        if (clamp_round) { v = round(v); if (v < lo) v = lo; if (v > hi) v = hi; }
        if (fout) fout[lin] = v; else if (bout) bout[lin] = (uint8_t)v;
        for (int d = ndim - 1; d >= 0; d--) { if (++idx[d] < shp[d]) break; idx[d] = 0; }
    }
    return { .array_num = out };
}

// load(path) -> (H, W, 4) ELEM_UINT8 RGBA image.
Item fn_load(Item path_item) {
    if (get_type_id(path_item) != LMD_TYPE_STRING) {
        log_error("load: expects a string file path"); return ItemError;
    }
    String* path = path_item.get_string();
    if (!path || path->len == 0) { log_error("load: empty path"); return ItemError; }
    int w = 0, h = 0, ch = 0;
    unsigned char* buf = image_load(path->chars, &w, &h, &ch, 0);  // always decodes to RGBA
    if (!buf) { log_error("load: failed to read image %s", path->chars); return ItemError; }
    int64_t shape[3] = { h, w, 4 };
    ArrayNum* out = alloc_ndim_arraynum(ELEM_UINT8, 3, shape);
    if (!out) { image_free(buf); return ItemError; }
    memcpy(out->data, buf, (size_t)h * (size_t)w * 4);
    image_free(buf);
    return { .array_num = out };
}

// save(img, path) -> writes a PNG; true on success.  Accepts 2-D (H,W) grayscale
// or 3-D (H,W,C) with C in {1,3,4}; float images are taken as [0,1] and scaled to
// [0,255], integer images are clamped directly.
Item fn_save(Item arr_item, Item path_item) {
    if (get_type_id(arr_item) != LMD_TYPE_ARRAY_NUM) {
        log_error("save: expects an image (typed numeric array)"); return ItemError;
    }
    if (get_type_id(path_item) != LMD_TYPE_STRING) {
        log_error("save: expects a string file path"); return ItemError;
    }
    String* path = path_item.get_string();
    if (!path || path->len == 0) { log_error("save: empty path"); return ItemError; }
    ArrayNum* in = arr_item.array_num;
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(in, shp, str);
    int64_t H, W, C;
    if (ndim == 2)      { H = shp[0]; W = shp[1]; C = 1; }
    else if (ndim == 3) { H = shp[0]; W = shp[1]; C = shp[2]; }
    else { log_error("save: image must be 2-D (H,W) or 3-D (H,W,C)"); return ItemError; }
    if (C != 1 && C != 3 && C != 4) {
        log_error("save: channel count must be 1, 3, or 4 (got %lld)", (long long)C); return ItemError;
    }
    // gather to a contiguous ubyte buffer (float [0,1] -> [0,255], int clamped)
    bool src_float = !elem_is_int(in->get_elem_type());
    Item ub = array_num_convert(in, ELEM_UINT8, src_float ? 255.0 : 1.0, true, 0.0, 255.0);
    if (get_type_id(ub) != LMD_TYPE_ARRAY_NUM) return ItemError;
    int ok = image_save_png(path->chars, (const unsigned char*)ub.array_num->data,
                            (int)W, (int)H, (int)C);
    if (!ok) { log_error("save: failed to write %s", path->chars); return ItemError; }
    return { .item = b2it(BOOL_TRUE) };
}

// as_float(img): ubyte [0,255] -> float [0,1] (passthrough if already float).
Item fn_as_float(Item arr_item) {
    if (get_type_id(arr_item) != LMD_TYPE_ARRAY_NUM) {
        log_error("as_float: expects a typed numeric array"); return ItemError;
    }
    ArrayNum* in = arr_item.array_num;
    double scale;
    switch (in->get_elem_type()) {
        case ELEM_UINT8:    scale = 1.0 / 255.0;   break;
        case ELEM_UINT8_CLAMPED: scale = 1.0 / 255.0; break;
        case ELEM_UINT16:   scale = 1.0 / 65535.0; break;
        case ELEM_FLOAT: case ELEM_FLOAT32: case ELEM_FLOAT64: scale = 1.0; break;  // passthrough
        default:            scale = 1.0 / 255.0;   break;  // assume 8-bit image range
    }
    return array_num_convert(in, ELEM_FLOAT, scale, false, 0.0, 0.0);
}

// as_ubyte(img): float [0,1] -> ubyte [0,255] (clamp+round; int images clamped).
Item fn_as_ubyte(Item arr_item) {
    if (get_type_id(arr_item) != LMD_TYPE_ARRAY_NUM) {
        log_error("as_ubyte: expects a typed numeric array"); return ItemError;
    }
    ArrayNum* in = arr_item.array_num;
    bool src_float = !elem_is_int(in->get_elem_type());
    return array_num_convert(in, ELEM_UINT8, src_float ? 255.0 : 1.0, true, 0.0, 255.0);
}

//==============================================================================
// Point, colour, and geometric image ops (Typed Array 4, Scope 1.2)
//
// Point/colour ops preserve shape and element type (ubyte stays ubyte with
// clamp+round, float stays float).  Geometric ops rearrange pixels into a fresh
// owned copy.  "white" is the saturated value for the representation: 255 for
// ubyte (65535 for u16), 1.0 for float — so the ops behave correctly in either.
//==============================================================================

static inline double image_white(ArrayNumElemType et) {
    switch (et) {
        case ELEM_UINT16: return 65535.0;
        case ELEM_FLOAT: case ELEM_FLOAT32: case ELEM_FLOAT64: return 1.0;
        default:          return 255.0;
    }
}

// Apply a per-element transform, preserving shape and elem type.  Walks an N-D
// odometer (so views map correctly); output is contiguous, written linearly.
template<typename Fn>
static Item array_num_point_op(ArrayNum* in, Fn fn) {
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(in, shp, str);
    if (ndim < 1) return ItemError;
    ArrayNum* out = alloc_ndim_arraynum(in->get_elem_type(), ndim, shp);
    if (!out) return ItemError;
    int64_t total = 1; for (int d = 0; d < ndim; d++) total *= shp[d];
    int64_t idx[32]; for (int d = 0; d < ndim; d++) idx[d] = 0;
    for (int64_t lin = 0; lin < total; lin++) {
        int64_t off = 0; for (int d = 0; d < ndim; d++) off += idx[d] * str[d];
        write_arr_elem_from_double(out, lin, fn(read_arr_elem_as_double(in, off)));
        for (int d = ndim - 1; d >= 0; d--) { if (++idx[d] < shp[d]) break; idx[d] = 0; }
    }
    return { .array_num = out };
}

// invert(img): photographic negative, white - v.
Item fn_invert(Item img) {
    if (get_type_id(img) != LMD_TYPE_ARRAY_NUM) { log_error("invert: expects an image array"); return ItemError; }
    double white = image_white(img.array_num->get_elem_type());
    return array_num_point_op(img.array_num, [white](double v){ return white - v; });
}

// gamma(img, g): gamma correction, white * (v/white)^g.
Item fn_gamma(Item img, Item gamma_item) {
    if (get_type_id(img) != LMD_TYPE_ARRAY_NUM) { log_error("gamma: expects an image array"); return ItemError; }
    double g = item_to_double(gamma_item);
    if (!(g > 0.0)) { log_error("gamma: exponent must be positive"); return ItemError; }
    double white = image_white(img.array_num->get_elem_type());
    return array_num_point_op(img.array_num, [white, g](double v){ return white * pow(v / white, g); });
}

// threshold(img, t): binarize — v >= t -> white, else 0.
Item fn_threshold(Item img, Item t_item) {
    if (get_type_id(img) != LMD_TYPE_ARRAY_NUM) { log_error("threshold: expects an image array"); return ItemError; }
    double t = item_to_double(t_item);
    double white = image_white(img.array_num->get_elem_type());
    return array_num_point_op(img.array_num, [t, white](double v){ return v >= t ? white : 0.0; });
}

// grayscale(img): (H,W,C>=3) -> (H,W) via Rec.601 luma; (H,W) or C<3 copied.
Item fn_grayscale(Item img) {
    if (get_type_id(img) != LMD_TYPE_ARRAY_NUM) { log_error("grayscale: expects an image array"); return ItemError; }
    ArrayNum* in = img.array_num;
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(in, shp, str);
    if (ndim == 2) return array_num_point_op(in, [](double v){ return v; });  // already 1-channel
    if (ndim != 3) { log_error("grayscale: image must be 2-D (H,W) or 3-D (H,W,C)"); return ItemError; }
    int64_t H = shp[0], W = shp[1], C = shp[2];
    int64_t oshape[2] = { H, W };
    ArrayNum* out = alloc_ndim_arraynum(in->get_elem_type(), 2, oshape);
    if (!out) return ItemError;
    int64_t lin = 0;
    for (int64_t i = 0; i < H; i++) {
        for (int64_t j = 0; j < W; j++) {
            int64_t base = i * str[0] + j * str[1];
            double gray;
            if (C >= 3) {
                gray = 0.299 * read_arr_elem_as_double(in, base)
                     + 0.587 * read_arr_elem_as_double(in, base + str[2])
                     + 0.114 * read_arr_elem_as_double(in, base + 2 * str[2]);
            } else {
                gray = read_arr_elem_as_double(in, base);  // single channel
            }
            write_arr_elem_from_double(out, lin++, gray);
        }
    }
    return { .array_num = out };
}

// Copy in[si, sj, :] for every output position, given a coordinate remap; the
// shared engine behind flip / rot90 / crop.  `out` must be a fresh contiguous
// (oH, oW[, C]) array; src(i,j) yields the input row/col for output (i,j).
template<typename Remap>
static Item array_num_remap(ArrayNum* in, int ndim, const int64_t* str, int64_t C,
                            ArrayNum* out, int64_t oH, int64_t oW, Remap src) {
    int64_t lin = 0;
    for (int64_t i = 0; i < oH; i++) {
        for (int64_t j = 0; j < oW; j++) {
            int64_t si, sj; src(i, j, &si, &sj);
            int64_t base = si * str[0] + sj * str[1];
            for (int64_t c = 0; c < C; c++) {
                int64_t off = base + ((ndim == 3) ? c * str[2] : 0);
                write_arr_elem_from_double(out, lin++, read_arr_elem_as_double(in, off));
            }
        }
    }
    return { .array_num = out };
}

// flip(img, axis): axis 0 = vertical (reverse rows), 1 = horizontal (reverse cols).
Item fn_flip(Item img, Item axis_item) {
    if (get_type_id(img) != LMD_TYPE_ARRAY_NUM) { log_error("flip: expects an image array"); return ItemError; }
    ArrayNum* in = img.array_num;
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(in, shp, str);
    if (ndim != 2 && ndim != 3) { log_error("flip: image must be 2-D or 3-D"); return ItemError; }
    int64_t axis = (int64_t)item_to_double(axis_item);
    if (axis != 0 && axis != 1) { log_error("flip: axis must be 0 (vertical) or 1 (horizontal)"); return ItemError; }
    int64_t H = shp[0], W = shp[1], C = (ndim == 3) ? shp[2] : 1;
    ArrayNum* out = alloc_ndim_arraynum(in->get_elem_type(), ndim, shp);
    if (!out) return ItemError;
    return array_num_remap(in, ndim, str, C, out, H, W,
        [axis, H, W](int64_t i, int64_t j, int64_t* si, int64_t* sj) {
            *si = (axis == 0) ? (H - 1 - i) : i;
            *sj = (axis == 1) ? (W - 1 - j) : j;
        });
}

// rot90(img, k): rotate counter-clockwise by 90*k degrees (k mod 4); odd k swaps H/W.
Item fn_rot90(Item img, Item k_item) {
    if (get_type_id(img) != LMD_TYPE_ARRAY_NUM) { log_error("rot90: expects an image array"); return ItemError; }
    ArrayNum* in = img.array_num;
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(in, shp, str);
    if (ndim != 2 && ndim != 3) { log_error("rot90: image must be 2-D or 3-D"); return ItemError; }
    int64_t k = ((int64_t)item_to_double(k_item)) % 4; if (k < 0) k += 4;
    int64_t H = shp[0], W = shp[1], C = (ndim == 3) ? shp[2] : 1;
    int64_t oH = (k & 1) ? W : H, oW = (k & 1) ? H : W;
    int64_t oshape[3] = { oH, oW, C };
    ArrayNum* out = alloc_ndim_arraynum(in->get_elem_type(), ndim, oshape);
    if (!out) return ItemError;
    return array_num_remap(in, ndim, str, C, out, oH, oW,
        [k, H, W](int64_t i, int64_t j, int64_t* si, int64_t* sj) {
            switch (k) {
                case 1:  *si = j;         *sj = W - 1 - i; break;  // CCW
                case 2:  *si = H - 1 - i; *sj = W - 1 - j; break;  // 180
                case 3:  *si = H - 1 - j; *sj = i;         break;  // CW
                default: *si = i;         *sj = j;         break;  // 0
            }
        });
}

// crop(img, rows, cols): owned copy of the inclusive region rows[r0..r1], cols[c0..c1]
// (rows/cols are ranges, e.g. crop(img, 0 to 9, 5 to 20)); bounds are clamped.
Item fn_crop(Item img, Item rrange, Item crange) {
    if (get_type_id(img) != LMD_TYPE_ARRAY_NUM) { log_error("crop: expects an image array"); return ItemError; }
    if (get_type_id(rrange) != LMD_TYPE_RANGE || get_type_id(crange) != LMD_TYPE_RANGE) {
        log_error("crop: row/col selectors must be ranges, e.g. crop(img, 0 to 9, 0 to 9)");
        return ItemError;
    }
    ArrayNum* in = img.array_num;
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(in, shp, str);
    if (ndim != 2 && ndim != 3) { log_error("crop: image must be 2-D or 3-D"); return ItemError; }
    int64_t H = shp[0], W = shp[1], C = (ndim == 3) ? shp[2] : 1;
    int64_t r0 = rrange.range->start, r1 = rrange.range->end;
    int64_t c0 = crange.range->start, c1 = crange.range->end;
    if (r0 < 0) r0 = 0;  if (c0 < 0) c0 = 0;
    if (r1 > H - 1) r1 = H - 1;  if (c1 > W - 1) c1 = W - 1;
    if (r1 < r0 || c1 < c0) { log_error("crop: empty region"); return ItemError; }
    int64_t oH = r1 - r0 + 1, oW = c1 - c0 + 1;
    int64_t oshape[3] = { oH, oW, C };
    ArrayNum* out = alloc_ndim_arraynum(in->get_elem_type(), ndim, oshape);
    if (!out) return ItemError;
    return array_num_remap(in, ndim, str, C, out, oH, oW,
        [r0, c0](int64_t i, int64_t j, int64_t* si, int64_t* sj) { *si = r0 + i; *sj = c0 + j; });
}

//==============================================================================
// Histogram / Otsu, connected components, and resize / warp (Typed Array 4, §1.4-1.6)
//==============================================================================

// histogram(img, bins) -> 1-D int counts.  Integer images use bincount (bin = the
// value); float images bin linearly over [0,1).  Out-of-range values clamp to the
// edge bins.  Basis for otsu and region-area measurement.
Item fn_histogram(Item img, Item bins_item) {
    if (get_type_id(img) != LMD_TYPE_ARRAY_NUM) { log_error("histogram: expects a numeric array"); return ItemError; }
    int64_t bins = (int64_t)item_to_double(bins_item);
    if (bins < 1) { log_error("histogram: bins must be >= 1"); return ItemError; }
    ArrayNum* in = img.array_num;
    bool is_float = !elem_is_int(in->get_elem_type());
    ArrayNum* counts = array_num_new(ELEM_INT, bins);
    if (!counts) return ItemError;
    for (int64_t i = 0; i < bins; i++) counts->items[i] = 0;
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(in, shp, str);
    int64_t total = 1; for (int d = 0; d < ndim; d++) total *= shp[d];
    int64_t idx[32]; for (int d = 0; d < ndim; d++) idx[d] = 0;
    for (int64_t n = 0; n < total; n++) {
        int64_t off = 0; for (int d = 0; d < ndim; d++) off += idx[d] * str[d];
        double v = read_arr_elem_as_double(in, off);
        int64_t bin = is_float ? (int64_t)floor(v * bins) : (int64_t)llround(v);
        if (bin < 0) bin = 0; else if (bin >= bins) bin = bins - 1;
        counts->items[bin]++;
        for (int d = ndim - 1; d >= 0; d--) { if (++idx[d] < shp[d]) break; idx[d] = 0; }
    }
    return { .array_num = counts };
}

// otsu(img) -> threshold value that maximizes between-class variance (256-bin).
// Foreground is `img > otsu(img)`.  Returns a value on the image's scale (0..255
// for ubyte/int, 0..1 for float); on a flat variance plateau the centre is taken.
Item fn_otsu(Item img) {
    if (get_type_id(img) != LMD_TYPE_ARRAY_NUM) { log_error("otsu: expects a numeric array"); return ItemError; }
    ArrayNum* in = img.array_num;
    const int64_t BINS = 256;
    bool is_float = !elem_is_int(in->get_elem_type());
    int64_t h[256]; for (int i = 0; i < BINS; i++) h[i] = 0;
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(in, shp, str);
    int64_t total = 1; for (int d = 0; d < ndim; d++) total *= shp[d];
    int64_t idx[32]; for (int d = 0; d < ndim; d++) idx[d] = 0;
    for (int64_t n = 0; n < total; n++) {
        int64_t off = 0; for (int d = 0; d < ndim; d++) off += idx[d] * str[d];
        double v = read_arr_elem_as_double(in, off);
        int64_t bin = is_float ? (int64_t)floor(v * BINS) : (int64_t)llround(v);
        if (bin < 0) bin = 0; else if (bin >= BINS) bin = BINS - 1;
        h[bin]++;
        for (int d = ndim - 1; d >= 0; d--) { if (++idx[d] < shp[d]) break; idx[d] = 0; }
    }
    double sum_all = 0; for (int i = 0; i < BINS; i++) sum_all += (double)i * h[i];
    double max_var = -1.0, sumB = 0; int64_t wB = 0, plateau_sum = 0, plateau_cnt = 0;
    for (int t = 0; t < BINS; t++) {
        wB += h[t];
        if (wB == 0) continue;
        int64_t wF = total - wB;
        if (wF == 0) break;
        sumB += (double)t * h[t];
        double mB = sumB / (double)wB, mF = (sum_all - sumB) / (double)wF;
        double var = (double)wB * (double)wF * (mB - mF) * (mB - mF);
        if (var > max_var)      { max_var = var; plateau_sum = t; plateau_cnt = 1; }
        else if (var == max_var) { plateau_sum += t; plateau_cnt++; }
    }
    int64_t best_t = plateau_cnt ? plateau_sum / plateau_cnt : 0;
    double white = image_white(in->get_elem_type());
    double thresh = (double)best_t / (double)(BINS - 1) * white;
    return push_d(thresh);
}

// label(mask) -> 2-D int image with 4-connected components numbered 1..N (0 = back-
// ground).  Any nonzero input pixel is foreground.  Region areas are then just
// histogram(labels, max(labels)+1); the component count is max(labels).
Item fn_label(Item mask_item) {
    if (get_type_id(mask_item) != LMD_TYPE_ARRAY_NUM) { log_error("label: expects a 2-D mask array"); return ItemError; }
    ArrayNum* in = mask_item.array_num;
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(in, shp, str);
    if (ndim != 2) { log_error("label: mask must be 2-D (H,W)"); return ItemError; }
    int64_t H = shp[0], W = shp[1];
    int64_t oshape[2] = { H, W };
    ArrayNum* out = alloc_ndim_arraynum(ELEM_INT, 2, oshape);
    if (!out) return ItemError;
    int64_t* lab = out->items;
    for (int64_t i = 0; i < H * W; i++) lab[i] = 0;
    int64_t* stack = (int64_t*)malloc(sizeof(int64_t) * (size_t)H * (size_t)W);
    if (!stack) { log_error("label: out of memory"); return ItemError; }
    const int64_t di[4] = { -1, 1, 0, 0 }, dj[4] = { 0, 0, -1, 1 };
    int64_t next = 1;
    for (int64_t i = 0; i < H; i++) {
        for (int64_t j = 0; j < W; j++) {
            int64_t oidx = i * W + j;
            if (lab[oidx] != 0) continue;
            if (read_arr_elem_as_double(in, i * str[0] + j * str[1]) == 0.0) continue;
            int64_t sp = 0; stack[sp++] = oidx; lab[oidx] = next;        // flood fill
            while (sp > 0) {
                int64_t cur = stack[--sp], ci = cur / W, cj = cur % W;
                for (int k = 0; k < 4; k++) {
                    int64_t ni = ci + di[k], nj = cj + dj[k];
                    if (ni < 0 || ni >= H || nj < 0 || nj >= W) continue;
                    int64_t nidx = ni * W + nj;
                    if (lab[nidx] != 0) continue;
                    if (read_arr_elem_as_double(in, ni * str[0] + nj * str[1]) == 0.0) continue;
                    lab[nidx] = next; stack[sp++] = nidx;
                }
            }
            next++;
        }
    }
    free(stack);
    return { .array_num = out };
}

// Bilinear sample of channel `c` at fractional (y, x).  edge_clamp replicates the
// border (for resize); otherwise positions outside the image return 0 (for warp).
static inline double bilinear_sample(ArrayNum* in, double y, double x, int64_t c,
                                     int64_t H, int64_t W, int ndim, const int64_t* str,
                                     bool edge_clamp) {
    if (!edge_clamp && (y < -0.5 || y > (double)H - 0.5 || x < -0.5 || x > (double)W - 0.5))
        return 0.0;
    int64_t y0 = (int64_t)floor(y), x0 = (int64_t)floor(x);
    double fy = y - (double)y0, fx = x - (double)x0;
    auto cl = [](int64_t v, int64_t n) { return v < 0 ? (int64_t)0 : v >= n ? n - 1 : v; };
    int64_t cy0 = cl(y0, H), cy1 = cl(y0 + 1, H), cx0 = cl(x0, W), cx1 = cl(x0 + 1, W);
    int64_t cc = (ndim == 3) ? c * str[2] : 0;
    double p00 = read_arr_elem_as_double(in, cy0 * str[0] + cx0 * str[1] + cc);
    double p01 = read_arr_elem_as_double(in, cy0 * str[0] + cx1 * str[1] + cc);
    double p10 = read_arr_elem_as_double(in, cy1 * str[0] + cx0 * str[1] + cc);
    double p11 = read_arr_elem_as_double(in, cy1 * str[0] + cx1 * str[1] + cc);
    return (p00 * (1 - fx) + p01 * fx) * (1 - fy) + (p10 * (1 - fx) + p11 * fx) * fy;
}

// Gather an output image by mapping each (oi, oj) to a fractional source (sy, sx)
// and bilinearly sampling.  The engine behind resize / rotate / affine_warp.
template<typename Map>
static Item bilinear_gather(ArrayNum* in, int ndim, const int64_t* str, int64_t H, int64_t W,
                            int64_t C, int64_t oH, int64_t oW, bool edge_clamp, Map map) {
    int64_t oshape[3] = { oH, oW, C };
    ArrayNum* out = alloc_ndim_arraynum(in->get_elem_type(), ndim, oshape);
    if (!out) return ItemError;
    int64_t lin = 0;
    for (int64_t oi = 0; oi < oH; oi++) {
        for (int64_t oj = 0; oj < oW; oj++) {
            double sy, sx; map(oi, oj, &sy, &sx);
            for (int64_t c = 0; c < C; c++)
                write_arr_elem_from_double(out, lin++, bilinear_sample(in, sy, sx, c, H, W, ndim, str, edge_clamp));
        }
    }
    return { .array_num = out };
}

// resize(img, new_h, new_w) -> bilinear-resampled image (half-pixel-centre mapping).
Item fn_resize(Item img, Item h_item, Item w_item) {
    if (get_type_id(img) != LMD_TYPE_ARRAY_NUM) { log_error("resize: expects an image array"); return ItemError; }
    ArrayNum* in = img.array_num;
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(in, shp, str);
    if (ndim != 2 && ndim != 3) { log_error("resize: image must be 2-D or 3-D"); return ItemError; }
    int64_t nH = (int64_t)item_to_double(h_item), nW = (int64_t)item_to_double(w_item);
    if (nH < 1 || nW < 1) { log_error("resize: target dimensions must be >= 1"); return ItemError; }
    int64_t H = shp[0], W = shp[1], C = (ndim == 3) ? shp[2] : 1;
    double sH = (double)H / (double)nH, sW = (double)W / (double)nW;
    return bilinear_gather(in, ndim, str, H, W, C, nH, nW, true,
        [sH, sW](int64_t oi, int64_t oj, double* sy, double* sx) {
            *sy = ((double)oi + 0.5) * sH - 0.5;
            *sx = ((double)oj + 0.5) * sW - 0.5;
        });
}

// rotate(img, deg) -> same-size image rotated counter-clockwise about its centre,
// bilinearly sampled; pixels mapping outside the source are 0.
Item fn_rotate(Item img, Item deg_item) {
    if (get_type_id(img) != LMD_TYPE_ARRAY_NUM) { log_error("rotate: expects an image array"); return ItemError; }
    ArrayNum* in = img.array_num;
    int64_t shp[32], str[32];
    int ndim = get_shape_strides(in, shp, str);
    if (ndim != 2 && ndim != 3) { log_error("rotate: image must be 2-D or 3-D"); return ItemError; }
    int64_t H = shp[0], W = shp[1], C = (ndim == 3) ? shp[2] : 1;
    const double PI = 3.14159265358979323846;
    double th = item_to_double(deg_item) * PI / 180.0;
    double ct = cos(th), st = sin(th), cy = (double)(H - 1) / 2.0, cx = (double)(W - 1) / 2.0;
    return bilinear_gather(in, ndim, str, H, W, C, H, W, false,
        [ct, st, cy, cx](int64_t oi, int64_t oj, double* sy, double* sx) {
            double dy = (double)oi - cy, dx = (double)oj - cx;   // inverse rotation R(-th)
            *sy = cy + dy * ct + dx * st;
            *sx = cx - dy * st + dx * ct;
        });
}

// affine_warp(img, M) -> same-size image gathered through a 2x3 affine matrix:
// [src_x, src_y]^T = M . [oj, oi, 1]^T.  Identity [[1,0,0],[0,1,0]] is a no-op.
Item fn_affine_warp(Item img, Item m_item) {
    if (get_type_id(img) != LMD_TYPE_ARRAY_NUM || get_type_id(m_item) != LMD_TYPE_ARRAY_NUM) {
        log_error("affine_warp: expects an image and a 2x3 numeric matrix"); return ItemError;
    }
    ArrayNum* in = img.array_num; ArrayNum* M = m_item.array_num;
    int64_t shp[32], str[32], mshp[32], mstr[32];
    int ndim = get_shape_strides(in, shp, str);
    int mndim = get_shape_strides(M, mshp, mstr);
    if (ndim != 2 && ndim != 3) { log_error("affine_warp: image must be 2-D or 3-D"); return ItemError; }
    if (mndim != 2 || mshp[0] != 2 || mshp[1] != 3) { log_error("affine_warp: matrix must be 2x3"); return ItemError; }
    int64_t H = shp[0], W = shp[1], C = (ndim == 3) ? shp[2] : 1;
    double a = read_arr_elem_as_double(M, 0), b = read_arr_elem_as_double(M, mstr[1]), c0 = read_arr_elem_as_double(M, 2 * mstr[1]);
    double d = read_arr_elem_as_double(M, mstr[0]), e = read_arr_elem_as_double(M, mstr[0] + mstr[1]), f = read_arr_elem_as_double(M, mstr[0] + 2 * mstr[1]);
    return bilinear_gather(in, ndim, str, H, W, C, H, W, false,
        [a, b, c0, d, e, f](int64_t oi, int64_t oj, double* sy, double* sx) {
            *sx = a * (double)oj + b * (double)oi + c0;
            *sy = d * (double)oj + e * (double)oi + f;
        });
}

// Overload wrappers — the sysfunc dispatcher resolves to fn_<name><argcount>.
// The 1-arg forms delegate to the existing whole-array reductions.
Item fn_sum1(Item arr)            { return fn_sum(arr); }
Item fn_sum2(Item arr, Item ax)   { return fn_sum_axis(arr, ax); }
Item fn_avg1(Item arr)            { return fn_avg(arr); }
Item fn_avg2(Item arr, Item ax)   { return get_type_id(ax) == LMD_TYPE_BOOL ? fn_avg_skip_null(arr, ax.bool_val) : fn_avg_axis(arr, ax); }
Item fn_math_prod1(Item arr)      { return fn_math_prod(arr); }
Item fn_math_prod2(Item arr, Item ax)   { return fn_prod_axis(arr, ax); }
Item fn_math_mean1(Item arr)      { return fn_math_mean(arr); }
Item fn_math_mean2(Item arr, Item ax)   { return get_type_id(ax) == LMD_TYPE_BOOL ? fn_math_mean_skip_null(arr, ax.bool_val) : fn_avg_axis(arr, ax); }
Item fn_math_median1(Item arr)    { return fn_math_median(arr); }
Item fn_math_median2(Item arr, Item option)    { return fn_math_median_skip_null(arr, is_skip_null_option(option)); }
Item fn_math_variance1(Item arr)  { return fn_math_variance(arr); }
Item fn_math_variance2(Item arr, Item option)  { return fn_math_variance_skip_null(arr, is_skip_null_option(option)); }
Item fn_math_deviation1(Item arr) { return fn_math_deviation(arr); }
Item fn_math_deviation2(Item arr, Item option) { return fn_math_deviation_skip_null(arr, is_skip_null_option(option)); }
Item fn_math_quantile2(Item arr, Item p) { return fn_math_quantile(arr, p); }
Item fn_math_quantile3(Item arr, Item p, Item option) { return fn_math_quantile_skip_null(arr, p, is_skip_null_option(option)); }
Item fn_math_cumsum1(Item arr)    { return fn_math_cumsum(arr); }
Item fn_math_cumsum2(Item arr, Item ax) { return fn_cumsum_axis(arr, ax); }
Item fn_math_cumprod1(Item arr)   { return fn_math_cumprod(arr); }
Item fn_math_cumprod2(Item arr, Item ax){ return fn_cumprod_axis(arr, ax); }

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
