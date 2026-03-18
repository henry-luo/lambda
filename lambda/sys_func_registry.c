// sys_func_registry.c — single source of truth for all system function metadata
// Phase 3: Unified Registry (configuration-driven code generation)
//
// This file consolidates:
//   - sys_funcs[] from build_ast.cpp (AST builder metadata)
//   - func_list[] fn_*/pn_* entries from mir.c (JIT import pointers)
//   - native_math_funcs[] from transpile.cpp (native C math optimization)
//   - native_binary_funcs[] from transpile.cpp (native binary func optimization)
//
// Adding a new system function now requires editing ONLY this file.
// When built for the dylib (LAMBDA_STATIC defined), all function pointers
// resolve to a dummy stub via FPTR()/NPTR() macros.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "sys_func_registry.h"  // includes lambda.h (brings in FPTR/NPTR macros)
#include "lambda-error.h"

// External Type globals (defined in lambda-data.cpp)
extern Type TYPE_NULL, TYPE_BOOL, TYPE_INT, TYPE_INT64, TYPE_FLOAT;
extern Type TYPE_STRING, TYPE_SYMBOL, TYPE_DTIME, TYPE_ANY, TYPE_ERROR, TYPE_TYPE;

// ============================================================================
// Runtime-only declarations (not needed for dylib / AST metadata builds)
// ============================================================================
#ifndef LAMBDA_STATIC
#include "../lib/stringbuf.h"
#include "lambda-path.h"

// aliased function declarations (actual names differ from JIT import names)
extern Symbol* fn_symbol(Item item);     // JIT name: fn_symbol1
extern Item fn_split(Item str, Item sep); // JIT name: fn_split2
extern Item fn_replace(Item str, Item old_str, Item new_str); // JIT name: fn_replace3

// target_equal is in target.cpp (C++ linkage)
extern bool target_equal(Target* a, Target* b);

// JS runtime functions
#include "js/js_runtime.h"
#include "js/js_dom.h"
#include "js/js_typed_array.h"

// shared runtime context (defined in mir.c)
extern Context* _lambda_rt;

// helper to access runtime pool from JIT code
static Pool* get_runtime_pool(void) {
    return _lambda_rt ? _lambda_rt->pool : NULL;
}

// helper functions for map pipe iteration in JIT
static int64_t pipe_map_len(void* keys_ptr) {
    ArrayList* keys = (ArrayList*)keys_ptr;
    return keys ? (int64_t)keys->length : 0;
}
static Item pipe_map_val(Item data, void* keys_ptr, int64_t index) {
    ArrayList* keys = (ArrayList*)keys_ptr;
    if (!keys || index >= (int64_t)keys->length) return ITEM_NULL;
    Symbol* key_sym = (Symbol*)keys->data[index];
    return item_attr(data, key_sym->chars);
}
static Item pipe_map_key(void* keys_ptr, int64_t index) {
    ArrayList* keys = (ArrayList*)keys_ptr;
    if (!keys || index >= (int64_t)keys->length) return ITEM_NULL;
    Symbol* key_sym = (Symbol*)keys->data[index];
    return y2it(key_sym);
}
#endif // !LAMBDA_STATIC


// ============================================================================
// The Single Source of Truth: sys_func_defs[]
// ============================================================================
// Fields: {id, name, arg_count, return_type, is_proc, is_overloaded,
//          is_method_eligible, first_param_type, can_raise,
//          c_ret_type, c_arg_conv,
//          c_func_name, func_ptr,
//          native_c_name, native_func_ptr, native_returns_float, native_arg_count}
//
// func_ptr: FPTR(fn_name) — resolves to real pointer or dummy stub
// native_func_ptr: NPTR(native_fn) — resolves to real pointer or dummy stub
// NULL means function is unimplemented or handled by special transpiler paths.

SysFuncInfo sys_func_defs[] = {
    // ========================================================================
    // Type/conversion functions — all method-eligible
    // ========================================================================
    {SYSFUNC_LEN, "len", 1, &TYPE_INT64, false, false, true, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_ITEM, "fn_len", FPTR(fn_len), NULL, NULL, false, 0},

    {SYSFUNC_TYPE, "type", 1, &TYPE_TYPE, false, false, true, LMD_TYPE_ANY, false,
     C_RET_TYPE_PTR, C_ARG_ITEM, "fn_type", FPTR(fn_type), NULL, NULL, false, 0},

    {SYSFUNC_NAME, "name", 1, &TYPE_SYMBOL, false, false, true, LMD_TYPE_ANY, false,
     C_RET_SYMBOL, C_ARG_ITEM, "fn_name", FPTR(fn_name), NULL, NULL, false, 0},

    {SYSFUNC_INT, "int", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_int", FPTR(fn_int), NULL, NULL, false, 0},

    {SYSFUNC_INT64, "int64", 1, &TYPE_INT64, false, false, true, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_ITEM, "fn_int64", FPTR(fn_int64), NULL, NULL, false, 0},

    {SYSFUNC_FLOAT, "float", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_float", FPTR(fn_float), NULL, NULL, false, 0},

    {SYSFUNC_DECIMAL, "decimal", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_decimal", FPTR(fn_decimal), NULL, NULL, false, 0},

    {SYSFUNC_BINARY, "binary", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_binary", FPTR(fn_binary), NULL, NULL, false, 0},

    {SYSFUNC_NUMBER, "number", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_number", NULL, NULL, NULL, false, 0},  // unimplemented

    {SYSFUNC_STRING, "string", 1, &TYPE_STRING, false, false, true, LMD_TYPE_ANY, false,
     C_RET_STRING, C_ARG_ITEM, "fn_string", FPTR(fn_string), NULL, NULL, false, 0},

    {SYSFUNC_SYMBOL, "symbol", 1, &TYPE_SYMBOL, false, true, true, LMD_TYPE_ANY, false,
     C_RET_SYMBOL, C_ARG_ITEM, "fn_symbol1", FPTR(fn_symbol), NULL, NULL, false, 0},

    {SYSFUNC_SYMBOL2, "symbol", 2, &TYPE_SYMBOL, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_symbol2", FPTR(fn_symbol2), NULL, NULL, false, 0},

    // ========================================================================
    // DateTime functions — overloaded with arg count suffix
    // ========================================================================
    {SYSFUNC_DATETIME0, "datetime", 0, &TYPE_DTIME, false, true, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_datetime0", FPTR(fn_datetime0), NULL, NULL, false, 0},

    {SYSFUNC_DATETIME, "datetime", 1, &TYPE_DTIME, false, true, true, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_datetime1", FPTR(fn_datetime1), NULL, NULL, false, 0},

    {SYSFUNC_DATE0, "date", 0, &TYPE_DTIME, false, true, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_date0", FPTR(fn_date0), NULL, NULL, false, 0},

    {SYSFUNC_DATE, "date", 1, &TYPE_DTIME, false, true, true, LMD_TYPE_DTIME, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_date1", FPTR(fn_date1), NULL, NULL, false, 0},

    {SYSFUNC_DATE3, "date", 3, &TYPE_DTIME, false, true, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_date3", FPTR(fn_date3), NULL, NULL, false, 0},

    {SYSFUNC_TIME0, "time", 0, &TYPE_DTIME, false, true, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_time0", FPTR(fn_time0), NULL, NULL, false, 0},

    {SYSFUNC_TIME, "time", 1, &TYPE_DTIME, false, true, true, LMD_TYPE_DTIME, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_time1", FPTR(fn_time1), NULL, NULL, false, 0},

    {SYSFUNC_TIME3, "time", 3, &TYPE_DTIME, false, true, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_time3", FPTR(fn_time3), NULL, NULL, false, 0},

    {SYSFUNC_JUSTNOW, "justnow", 0, &TYPE_DTIME, false, false, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_justnow", FPTR(fn_justnow), NULL, NULL, false, 0},

    // ========================================================================
    // Collection functions
    // ========================================================================
    {SYSFUNC_SET, "set", -1, &TYPE_ANY, false, false, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_set", NULL, NULL, NULL, false, 0},  // variadic, unimplemented

    {SYSFUNC_SLICE, "slice", 3, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_slice", FPTR(fn_slice), NULL, NULL, false, 0},

    {SYSFUNC_ALL, "all", 1, &TYPE_BOOL, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_all", NULL, NULL, NULL, false, 0},  // unimplemented

    {SYSFUNC_ANY, "any", 1, &TYPE_BOOL, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_any", NULL, NULL, NULL, false, 0},  // unimplemented

    // min/max — 1-arg is method-eligible, 2-arg is not
    {SYSFUNC_MIN1, "min", 1, &TYPE_ANY, false, true, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_min1", FPTR(fn_min1), NULL, NULL, false, 0},

    {SYSFUNC_MIN2, "min", 2, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_min2", FPTR(fn_min2), "fn_min2_u", NPTR(fn_min2_u), true, 2},

    {SYSFUNC_MAX1, "max", 1, &TYPE_ANY, false, true, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_max1", FPTR(fn_max1), NULL, NULL, false, 0},

    {SYSFUNC_MAX2, "max", 2, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_max2", FPTR(fn_max2), "fn_max2_u", NPTR(fn_max2_u), true, 2},

    // Aggregation functions — method-eligible on collections
    {SYSFUNC_SUM, "sum", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_sum", FPTR(fn_sum), NULL, NULL, false, 0},

    {SYSFUNC_AVG, "avg", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_avg", FPTR(fn_avg), NULL, NULL, false, 0},

    // ========================================================================
    // Math functions — method-eligible on numbers
    // ========================================================================
    {SYSFUNC_ABS, "abs", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_abs", FPTR(fn_abs), "fabs", NPTR(fabs), true, 1},

    {SYSFUNC_ROUND, "round", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_round", FPTR(fn_round), "round", NPTR(round), true, 1},

    {SYSFUNC_FLOOR, "floor", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_floor", FPTR(fn_floor), "floor", NPTR(floor), true, 1},

    {SYSFUNC_CEIL, "ceil", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_ceil", FPTR(fn_ceil), "ceil", NPTR(ceil), true, 1},

    // ========================================================================
    // I/O functions — can_raise=true for functions that may fail
    // ========================================================================
    {SYSFUNC_INPUT1, "input", 1, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "fn_input1", FPTR(fn_input1), NULL, NULL, false, 0},

    {SYSFUNC_INPUT2, "input", 2, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "fn_input2", FPTR(fn_input2), NULL, NULL, false, 0},

    {SYSFUNC_FORMAT1, "format", 1, &TYPE_STRING, false, true, true, LMD_TYPE_ANY, false,
     C_RET_STRING, C_ARG_ITEM, "fn_format1", FPTR(fn_format1), NULL, NULL, false, 0},

    {SYSFUNC_FORMAT2, "format", 2, &TYPE_STRING, false, true, true, LMD_TYPE_ANY, false,
     C_RET_STRING, C_ARG_ITEM, "fn_format2", FPTR(fn_format2), NULL, NULL, false, 0},

    {SYSFUNC_ERROR, "error", 1, &TYPE_ERROR, false, false, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_error", FPTR(fn_error), NULL, NULL, false, 0},

    // ========================================================================
    // String functions — method-eligible on strings
    // ========================================================================
    {SYSFUNC_NORMALIZE, "normalize", 1, &TYPE_STRING, false, true, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_normalize1", FPTR(fn_normalize1), NULL, NULL, false, 0},

    {SYSFUNC_NORMALIZE2, "normalize", 2, &TYPE_STRING, false, true, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_normalize2", FPTR(fn_normalize), NULL, NULL, false, 0},

    {SYSFUNC_CONTAINS, "contains", 2, &TYPE_BOOL, false, false, true, LMD_TYPE_STRING, false,
     C_RET_BOOL, C_ARG_ITEM, "fn_contains", FPTR(fn_contains), NULL, NULL, false, 0},

    {SYSFUNC_STARTS_WITH, "starts_with", 2, &TYPE_BOOL, false, false, true, LMD_TYPE_STRING, false,
     C_RET_BOOL, C_ARG_ITEM, "fn_starts_with", FPTR(fn_starts_with), NULL, NULL, false, 0},

    {SYSFUNC_ENDS_WITH, "ends_with", 2, &TYPE_BOOL, false, false, true, LMD_TYPE_STRING, false,
     C_RET_BOOL, C_ARG_ITEM, "fn_ends_with", FPTR(fn_ends_with), NULL, NULL, false, 0},

    {SYSFUNC_INDEX_OF, "index_of", 2, &TYPE_INT64, false, false, true, LMD_TYPE_STRING, false,
     C_RET_INT64, C_ARG_ITEM, "fn_index_of", FPTR(fn_index_of), NULL, NULL, false, 0},

    {SYSFUNC_LAST_INDEX_OF, "last_index_of", 2, &TYPE_INT64, false, false, true, LMD_TYPE_STRING, false,
     C_RET_INT64, C_ARG_ITEM, "fn_last_index_of", FPTR(fn_last_index_of), NULL, NULL, false, 0},

    {SYSFUNC_TRIM, "trim", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_trim", FPTR(fn_trim), NULL, NULL, false, 0},

    {SYSFUNC_TRIM_START, "trim_start", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_trim_start", FPTR(fn_trim_start), NULL, NULL, false, 0},

    {SYSFUNC_TRIM_END, "trim_end", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_trim_end", FPTR(fn_trim_end), NULL, NULL, false, 0},

    {SYSFUNC_LOWER, "lower", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_lower", FPTR(fn_lower), NULL, NULL, false, 0},

    {SYSFUNC_UPPER, "upper", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_upper", FPTR(fn_upper), NULL, NULL, false, 0},

    {SYSFUNC_URL_RESOLVE, "url_resolve", 2, &TYPE_STRING, false, false, false, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_url_resolve", FPTR(fn_url_resolve), NULL, NULL, false, 0},

    {SYSFUNC_SPLIT, "split", 2, &TYPE_ANY, false, true, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_split2", FPTR(fn_split), NULL, NULL, false, 0},

    {SYSFUNC_SPLIT3, "split", 3, &TYPE_ANY, false, true, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_split3", FPTR(fn_split3), NULL, NULL, false, 0},

    {SYSFUNC_JOIN, "join", 2, &TYPE_ANY, false, true, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_join2", FPTR(fn_join2), NULL, NULL, false, 0},

    {SYSFUNC_REPLACE, "replace", 3, &TYPE_ANY, false, true, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_replace3", FPTR(fn_replace), NULL, NULL, false, 0},

    {SYSFUNC_FIND, "find", 2, &TYPE_ANY, false, true, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_find2", FPTR(fn_find2), NULL, NULL, false, 0},

    {SYSFUNC_FIND3, "find", 3, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_find3", FPTR(fn_find3), NULL, NULL, false, 0},

    {SYSFUNC_ORD, "ord", 1, &TYPE_INT64, false, false, false, LMD_TYPE_STRING, false,
     C_RET_INT64, C_ARG_ITEM, "fn_ord", FPTR(fn_ord), NULL, NULL, false, 0},

    {SYSFUNC_CHR, "chr", 1, &TYPE_STRING, false, false, false, LMD_TYPE_INT, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_chr", FPTR(fn_chr), NULL, NULL, false, 0},

    // ========================================================================
    // Vector/array functions — math module
    // ========================================================================
    {SYSFUNC_PROD, "math_prod", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_prod", FPTR(fn_math_prod), NULL, NULL, false, 0},

    {SYSFUNC_CUMSUM, "math_cumsum", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_cumsum", FPTR(fn_math_cumsum), NULL, NULL, false, 0},

    {SYSFUNC_CUMPROD, "math_cumprod", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_cumprod", FPTR(fn_math_cumprod), NULL, NULL, false, 0},

    {SYSFUNC_ARGMIN, "argmin", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_argmin", FPTR(fn_argmin), NULL, NULL, false, 0},

    {SYSFUNC_ARGMAX, "argmax", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_argmax", FPTR(fn_argmax), NULL, NULL, false, 0},

    {SYSFUNC_FILL, "fill", 2, &TYPE_ANY, false, false, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_fill", FPTR(fn_fill), NULL, NULL, false, 0},

    {SYSFUNC_DOT, "math_dot", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_dot", FPTR(fn_math_dot), NULL, NULL, false, 0},

    {SYSFUNC_NORM, "math_norm", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_norm", FPTR(fn_math_norm), NULL, NULL, false, 0},

    // ========================================================================
    // Statistical functions — math module
    // ========================================================================
    {SYSFUNC_MEAN, "math_mean", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_mean", FPTR(fn_math_mean), NULL, NULL, false, 0},

    {SYSFUNC_MEDIAN, "math_median", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_median", FPTR(fn_math_median), NULL, NULL, false, 0},

    {SYSFUNC_VARIANCE, "math_variance", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_variance", FPTR(fn_math_variance), NULL, NULL, false, 0},

    {SYSFUNC_DEVIATION, "math_deviation", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_deviation", FPTR(fn_math_deviation), NULL, NULL, false, 0},

    // ========================================================================
    // Element-wise math functions — math module (with native C math optimization)
    // ========================================================================
    {SYSFUNC_SQRT, "math_sqrt", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_sqrt", FPTR(fn_math_sqrt), "sqrt", NPTR(sqrt), true, 1},

    {SYSFUNC_LOG, "math_log", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_log", FPTR(fn_math_log), "log", NPTR(log), true, 1},

    {SYSFUNC_LOG10, "math_log10", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_log10", FPTR(fn_math_log10), "log10", NPTR(log10), true, 1},

    {SYSFUNC_EXP, "math_exp", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_exp", FPTR(fn_math_exp), "exp", NPTR(exp), true, 1},

    {SYSFUNC_SIN, "math_sin", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_sin", FPTR(fn_math_sin), "sin", NPTR(sin), true, 1},

    {SYSFUNC_COS, "math_cos", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_cos", FPTR(fn_math_cos), "cos", NPTR(cos), true, 1},

    {SYSFUNC_TAN, "math_tan", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_tan", FPTR(fn_math_tan), "tan", NPTR(tan), true, 1},

    // inverse trigonometric
    {SYSFUNC_ASIN, "math_asin", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_asin", FPTR(fn_math_asin), "asin", NPTR(asin), true, 1},

    {SYSFUNC_ACOS, "math_acos", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_acos", FPTR(fn_math_acos), "acos", NPTR(acos), true, 1},

    {SYSFUNC_ATAN, "math_atan", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_atan", FPTR(fn_math_atan), "atan", NPTR(atan), true, 1},

    {SYSFUNC_ATAN2, "math_atan2", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_atan2", FPTR(fn_math_atan2), "atan2", NPTR(atan2), true, 2},

    // hyperbolic
    {SYSFUNC_SINH, "math_sinh", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_sinh", FPTR(fn_math_sinh), "sinh", NPTR(sinh), true, 1},

    {SYSFUNC_COSH, "math_cosh", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_cosh", FPTR(fn_math_cosh), "cosh", NPTR(cosh), true, 1},

    {SYSFUNC_TANH, "math_tanh", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_tanh", FPTR(fn_math_tanh), "tanh", NPTR(tanh), true, 1},

    // inverse hyperbolic
    {SYSFUNC_ASINH, "math_asinh", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_asinh", FPTR(fn_math_asinh), "asinh", NPTR(asinh), true, 1},

    {SYSFUNC_ACOSH, "math_acosh", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_acosh", FPTR(fn_math_acosh), "acosh", NPTR(acosh), true, 1},

    {SYSFUNC_ATANH, "math_atanh", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_atanh", FPTR(fn_math_atanh), "atanh", NPTR(atanh), true, 1},

    // exponential/logarithmic variants
    {SYSFUNC_EXP2, "math_exp2", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_exp2", FPTR(fn_math_exp2), "exp2", NPTR(exp2), true, 1},

    {SYSFUNC_EXPM1, "math_expm1", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_expm1", FPTR(fn_math_expm1), "expm1", NPTR(expm1), true, 1},

    {SYSFUNC_LOG2, "math_log2", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_log2", FPTR(fn_math_log2), "log2", NPTR(log2), true, 1},

    // power/root
    {SYSFUNC_POW_MATH, "math_pow", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_pow", FPTR(fn_math_pow), "fn_pow_u", NPTR(fn_pow_u), true, 2},

    {SYSFUNC_CBRT, "math_cbrt", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_cbrt", FPTR(fn_math_cbrt), "cbrt", NPTR(cbrt), true, 1},

    {SYSFUNC_TRUNC, "math_trunc", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_trunc", FPTR(fn_math_trunc), "trunc", NPTR(trunc), true, 1},

    {SYSFUNC_HYPOT, "math_hypot", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_hypot", FPTR(fn_math_hypot), "hypot", NPTR(hypot), true, 2},

    {SYSFUNC_LOG1P, "math_log1p", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_log1p", FPTR(fn_math_log1p), "log1p", NPTR(log1p), true, 1},

    {SYSFUNC_SIGN, "sign", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_sign", FPTR(fn_sign), NULL, NULL, false, 0},

    // random number generation (pure functional, SplitMix64)
    {SYSFUNC_RANDOM, "math_random", 1, &TYPE_ANY, false, false, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_random", FPTR(fn_math_random), NULL, NULL, false, 0},

    // ========================================================================
    // Vector manipulation functions — method-eligible on collections
    // ========================================================================
    {SYSFUNC_REVERSE, "reverse", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_reverse", FPTR(fn_reverse), NULL, NULL, false, 0},

    {SYSFUNC_SORT, "sort", 1, &TYPE_ANY, false, true, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_sort1", FPTR(fn_sort1), NULL, NULL, false, 0},

    {SYSFUNC_SORT2, "sort", 2, &TYPE_ANY, false, true, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_sort2", FPTR(fn_sort2), NULL, NULL, false, 0},

    {SYSFUNC_UNIQUE, "unique", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_unique", FPTR(fn_unique), NULL, NULL, false, 0},

    {SYSFUNC_CONCAT, "concat", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_concat", FPTR(fn_concat), NULL, NULL, false, 0},

    {SYSFUNC_TAKE, "take", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_take", FPTR(fn_take), NULL, NULL, false, 0},

    {SYSFUNC_DROP, "drop", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_drop", FPTR(fn_drop), NULL, NULL, false, 0},

    {SYSFUNC_ZIP, "zip", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_zip", FPTR(fn_zip), NULL, NULL, false, 0},

    {SYSFUNC_RANGE3, "range", 3, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_range3", FPTR(fn_range3), NULL, NULL, false, 0},

    {SYSFUNC_QUANTILE, "math_quantile", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_quantile", FPTR(fn_math_quantile), NULL, NULL, false, 0},

    {SYSFUNC_REDUCE, "reduce", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_reduce", FPTR(fn_reduce), NULL, NULL, false, 0},

    // ========================================================================
    // Parse string functions — overloaded with arg count
    // ========================================================================
    {SYSFUNC_PARSE1, "parse", 1, &TYPE_ANY, false, true, true, LMD_TYPE_STRING, true,
     C_RET_RETITEM, C_ARG_ITEM, "fn_parse1", FPTR(fn_parse1), NULL, NULL, false, 0},

    {SYSFUNC_PARSE2, "parse", 2, &TYPE_ANY, false, true, true, LMD_TYPE_STRING, true,
     C_RET_RETITEM, C_ARG_ITEM, "fn_parse2", FPTR(fn_parse2), NULL, NULL, false, 0},

    // ========================================================================
    // Variadic parameter access — not method-eligible
    // ========================================================================
    {SYSFUNC_VARG0, "varg", 0, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_varg0", FPTR(fn_varg0), NULL, NULL, false, 0},

    {SYSFUNC_VARG1, "varg", 1, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_varg1", FPTR(fn_varg1), NULL, NULL, false, 0},

    // ========================================================================
    // Procedural functions — not method-eligible (side effects)
    // ========================================================================
    {SYSPROC_NOW, "now", 0, &TYPE_DTIME, true, false, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "pn_now", NULL, NULL, NULL, false, 0},  // unimplemented

    {SYSPROC_TODAY, "today", 0, &TYPE_DTIME, true, false, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "pn_today", NULL, NULL, NULL, false, 0},  // unimplemented

    {SYSPROC_PRINT, "print", 1, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "pn_print", FPTR(pn_print), NULL, NULL, false, 0},

    {SYSPROC_CLOCK, "clock", 0, &TYPE_FLOAT, true, false, false, LMD_TYPE_ANY, false,
     C_RET_DOUBLE, C_ARG_ITEM, "pn_clock", FPTR(pn_clock), NULL, NULL, false, 0},

    {SYSPROC_FETCH, "fetch", 2, &TYPE_ANY, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_fetch", FPTR(pn_fetch), NULL, NULL, false, 0},

    {SYSPROC_OUTPUT2, "output", 2, &TYPE_ANY, true, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_output2", FPTR(pn_output2), NULL, NULL, false, 0},

    {SYSPROC_OUTPUT3, "output", 3, &TYPE_ANY, true, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_output3", FPTR(pn_output3), NULL, NULL, false, 0},

    {SYSPROC_CMD1, "cmd", 1, &TYPE_ANY, true, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_cmd1", FPTR(pn_cmd1), NULL, NULL, false, 0},

    {SYSPROC_CMD, "cmd", 2, &TYPE_ANY, true, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_cmd2", FPTR(pn_cmd2), NULL, NULL, false, 0},

    // ========================================================================
    // IO module procedures — all can_raise=true for I/O errors
    // ========================================================================
    {SYSPROC_IO_COPY, "io_copy", 2, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_copy", FPTR(pn_io_copy), NULL, NULL, false, 0},

    {SYSPROC_IO_MOVE, "io_move", 2, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_move", FPTR(pn_io_move), NULL, NULL, false, 0},

    {SYSPROC_IO_DELETE, "io_delete", 1, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_delete", FPTR(pn_io_delete), NULL, NULL, false, 0},

    {SYSPROC_IO_MKDIR, "io_mkdir", 1, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_mkdir", FPTR(pn_io_mkdir), NULL, NULL, false, 0},

    {SYSPROC_IO_TOUCH, "io_touch", 1, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_touch", FPTR(pn_io_touch), NULL, NULL, false, 0},

    {SYSPROC_IO_SYMLINK, "io_symlink", 2, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_symlink", FPTR(pn_io_symlink), NULL, NULL, false, 0},

    {SYSPROC_IO_CHMOD, "io_chmod", 2, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_chmod", FPTR(pn_io_chmod), NULL, NULL, false, 0},

    {SYSPROC_IO_RENAME, "io_rename", 2, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_rename", FPTR(pn_io_rename), NULL, NULL, false, 0},

    {SYSPROC_IO_FETCH, "io_fetch", 1, &TYPE_ANY, true, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_fetch1", FPTR(pn_io_fetch1), NULL, NULL, false, 0},

    {SYSPROC_IO_FETCH, "io_fetch", 2, &TYPE_ANY, true, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_fetch2", FPTR(pn_io_fetch2), NULL, NULL, false, 0},

    {SYSFUNC_EXISTS, "exists", 1, &TYPE_BOOL, false, false, false, LMD_TYPE_ANY, false,
     C_RET_BOOL, C_ARG_ITEM, "fn_exists", FPTR(fn_exists), NULL, NULL, false, 0},

    // ========================================================================
    // Bitwise functions — operate on integers, not method-eligible
    // ========================================================================
    {SYSFUNC_BAND, "band", 2, &TYPE_INT, false, false, false, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_NATIVE, "fn_band", FPTR(fn_band), NULL, NULL, false, 0},

    {SYSFUNC_BOR, "bor", 2, &TYPE_INT, false, false, false, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_NATIVE, "fn_bor", FPTR(fn_bor), NULL, NULL, false, 0},

    {SYSFUNC_BXOR, "bxor", 2, &TYPE_INT, false, false, false, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_NATIVE, "fn_bxor", FPTR(fn_bxor), NULL, NULL, false, 0},

    {SYSFUNC_BNOT, "bnot", 1, &TYPE_INT, false, false, false, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_NATIVE, "fn_bnot", FPTR(fn_bnot), NULL, NULL, false, 0},

    {SYSFUNC_SHL, "shl", 2, &TYPE_INT, false, false, false, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_NATIVE, "fn_shl", FPTR(fn_shl), NULL, NULL, false, 0},

    {SYSFUNC_SHR, "shr", 2, &TYPE_INT, false, false, false, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_NATIVE, "fn_shr", FPTR(fn_shr), NULL, NULL, false, 0},

    // ========================================================================
    // VMap functions — handled by special transpiler paths
    // ========================================================================
    {SYSFUNC_VMAP_NEW, "map", 0, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_map0", NULL, NULL, NULL, false, 0},  // transpiler special case

    {SYSFUNC_VMAP_NEW, "map", 1, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_map1", NULL, NULL, NULL, false, 0},  // transpiler special case

    {SYSPROC_VMAP_SET, "set", 3, &TYPE_NULL, true, true, true, LMD_TYPE_VMAP, false,
     C_RET_ITEM, C_ARG_ITEM, "pn_set3", NULL, NULL, NULL, false, 0},  // transpiler special case

    // ========================================================================
    // Replace-in-file procedures (sed-like) — NOT YET IMPLEMENTED
    // Commented out: same key ("replace", 3) as SYSFUNC_REPLACE above.
    // Needs first_param_type-based disambiguation in build_ast before enabling.
    // ========================================================================
    // {SYSPROC_REPLACE_FILE, "replace", 3, &TYPE_NULL, true, true, false, LMD_TYPE_PATH, true,
    //  C_RET_RETITEM, C_ARG_ITEM, "pn_replace_file3", NULL, NULL, NULL, false, 0},
    // {SYSPROC_REPLACE_FILE4, "replace", 4, &TYPE_NULL, true, true, false, LMD_TYPE_PATH, true,
    //  C_RET_RETITEM, C_ARG_ITEM, "pn_replace_file4", NULL, NULL, NULL, false, 0},
};

// note: sizeof(sys_func_defs) may fail with incomplete type because the header
// declares extern SysFuncInfo sys_func_defs[]. Use a forward-declared count instead.
#define SYS_FUNC_DEF_COUNT (sizeof(sys_func_defs) / sizeof(SysFuncInfo))
const int sys_func_def_count = SYS_FUNC_DEF_COUNT;


// ============================================================================
// JIT Runtime Imports: non-sys-func entries for MIR import resolution
// ============================================================================
// These are operators, runtime infrastructure, JS functions, etc. that the
// transpiler emits calls to but are not system functions (no AST metadata).
// Only needed for the main executable build (JIT compilation).

#ifndef LAMBDA_STATIC

// MIR JIT wrapper declarations for RetItem-returning functions
extern Item fn_parse1_mir(Item str_item);
extern Item fn_parse2_mir(Item str_item, Item type);
extern Item fn_input1_mir(Item url);
extern Item fn_input2_mir(Item url, Item options);
extern Item pn_cmd1_mir(Item cmd);
extern Item pn_cmd2_mir(Item cmd, Item args);
extern Item pn_fetch_mir(Item url, Item options);
extern Item pn_output2_mir(Item source, Item target);
extern Item pn_output3_mir(Item source, Item target, Item options);
extern Item pn_io_copy_mir(Item src, Item dst);
extern Item pn_io_move_mir(Item src, Item dst);
extern Item pn_io_delete_mir(Item path);
extern Item pn_io_mkdir_mir(Item path);
extern Item pn_io_touch_mir(Item path);
extern Item pn_io_symlink_mir(Item target, Item link);
extern Item pn_io_chmod_mir(Item path, Item mode);
extern Item pn_io_rename_mir(Item old_path, Item new_path);
extern Item pn_io_fetch1_mir(Item target);
extern Item pn_io_fetch2_mir(Item target, Item options);
extern Item pn_output_append_mir(Item source, Item target);

// Trampolines for calling _b boxed wrappers from MIR Direct (RetItem ABI fix)
extern Item fn_call_boxed_0(void* fp);
extern Item fn_call_boxed_1(void* fp, Item a);
extern Item fn_call_boxed_2(void* fp, Item a, Item b);
extern Item fn_call_boxed_3(void* fp, Item a, Item b, Item c);
extern Item fn_call_boxed_4(void* fp, Item a, Item b, Item c, Item d);
extern Item fn_call_boxed_5(void* fp, Item a, Item b, Item c, Item d, Item e);
extern Item fn_call_boxed_6(void* fp, Item a, Item b, Item c, Item d, Item e, Item f);
extern Item fn_call_boxed_7(void* fp, Item a, Item b, Item c, Item d, Item e, Item f, Item g);
extern Item fn_call_boxed_8(void* fp, Item a, Item b, Item c, Item d, Item e, Item f, Item g, Item h);

JitImport jit_runtime_imports[] = {
    // C library functions
    {"memset", FPTR(memset)},
    {"memcpy", FPTR(memcpy)},
    {"fmod", FPTR(fmod)},
    // stack overflow protection
    {"lambda_stack_overflow_error", FPTR(lambda_stack_overflow_error)},

    // ========================================================================
    // Array constructors and operations
    // ========================================================================
    {"array", FPTR(array)},
    {"array_int", FPTR(array_int)},
    {"array_int64", FPTR(array_int64)},
    {"array_float", FPTR(array_float)},
    {"array_fill", FPTR(array_fill)},
    {"array_int_fill", FPTR(array_int_fill)},
    {"array_int64_fill", FPTR(array_int64_fill)},
    {"array_float_fill", FPTR(array_float_fill)},
    {"array_get", FPTR(array_get)},
    {"array_int_get", FPTR(array_int_get)},
    {"array_int64_get", FPTR(array_int64_get)},
    {"array_float_get", FPTR(array_float_get)},
    {"array_int_get_raw", FPTR(array_int_get_raw)},
    {"array_int64_get_raw", FPTR(array_int64_get_raw)},
    {"array_float_get_value", FPTR(array_float_get_value)},
    {"array_spreadable", FPTR(array_spreadable)},
    {"array_plain", FPTR(array_plain)},
    {"array_drop_inplace", FPTR(array_drop_inplace)},
    {"array_limit_inplace", FPTR(array_limit_inplace)},
    {"array_push", FPTR(array_push)},
    {"array_push_spread", FPTR(array_push_spread)},
    {"array_end", FPTR(array_end)},
    {"item_spread", FPTR(item_spread)},
    // typed array constructors
    {"array_float_new", FPTR(array_float_new)},
    {"array_float_set", FPTR(array_float_set)},
    {"array_int_new", FPTR(array_int_new)},
    {"array_int_set", FPTR(array_int_set)},

    // ========================================================================
    // List operations
    // ========================================================================
    {"list", FPTR(list)},
    {"list_fill", FPTR(list_fill)},
    {"list_push", FPTR(list_push)},
    {"list_push_spread", FPTR(list_push_spread)},
    {"list_get", FPTR(list_get)},
    {"list_end", FPTR(list_end)},

    // ========================================================================
    // Map, Element, Object operations
    // ========================================================================
    {"map", FPTR(map)},
    {"map_with_data", FPTR(map_with_data)},
    {"map_fill", FPTR(map_fill)},
    {"map_get", FPTR(map_get)},
    {"elmt", FPTR(elmt)},
    {"elmt_fill", FPTR(elmt_fill)},
    {"elmt_get", FPTR(elmt_get)},
    {"object", FPTR(object)},
    {"object_with_data", FPTR(object_with_data)},
    {"object_fill", FPTR(object_fill)},
    {"object_get", FPTR(object_get)},
    {"object_type_set_method", FPTR(object_type_set_method)},
    {"object_type_set_constraint", FPTR(object_type_set_constraint)},

    // ========================================================================
    // Boxing / unboxing / type checks
    // ========================================================================
    {"is_truthy", FPTR(is_truthy)},
    {"v2it", FPTR(v2it)},
    {"push_d", FPTR(push_d)},
    {"push_l", FPTR(push_l)},
    {"push_l_safe", FPTR(push_l_safe)},
    {"push_d_safe", FPTR(push_d_safe)},
    {"push_k", FPTR(push_k)},
    {"push_k_safe", FPTR(push_k_safe)},
    {"push_c", FPTR(push_c)},
    {"item_keys", FPTR(item_keys)},
    {"item_attr", FPTR(item_attr)},
    {"item_type_id", FPTR(item_type_id)},
    {"item_at", FPTR(item_at)},
    {"it2l", FPTR(it2l)},
    {"it2d", FPTR(it2d)},
    {"it2i", FPTR(it2i)},
    {"it2b", FPTR(it2b)},
    {"it2s", FPTR(it2s)},
    {"fn_to_cstr", FPTR(fn_to_cstr)},
    {"ensure_typed_array", FPTR(ensure_typed_array)},

    // ========================================================================
    // Arithmetic and comparison operators
    // ========================================================================
    {"fn_add", FPTR(fn_add)},
    {"fn_sub", FPTR(fn_sub)},
    {"fn_mul", FPTR(fn_mul)},
    {"fn_div", FPTR(fn_div)},
    {"fn_idiv", FPTR(fn_idiv)},
    {"fn_mod", FPTR(fn_mod)},
    {"fn_pow", FPTR(fn_pow)},
    {"fn_pos", FPTR(fn_pos)},
    {"fn_neg", FPTR(fn_neg)},
    {"fn_eq", FPTR(fn_eq)},
    {"fn_ne", FPTR(fn_ne)},
    {"fn_str_eq_ptr", FPTR(fn_str_eq_ptr)},
    {"fn_sym_eq_ptr", FPTR(fn_sym_eq_ptr)},
    {"fn_lt", FPTR(fn_lt)},
    {"fn_gt", FPTR(fn_gt)},
    {"fn_le", FPTR(fn_le)},
    {"fn_ge", FPTR(fn_ge)},
    {"fn_not", FPTR(fn_not)},
    {"fn_and", FPTR(fn_and)},
    {"fn_or", FPTR(fn_or)},
    {"op_and", FPTR(op_and)},
    {"op_or", FPTR(op_or)},
    {"fn_is", FPTR(fn_is)},
    {"fn_is_nan", FPTR(fn_is_nan)},
    {"fn_in", FPTR(fn_in)},

    // ========================================================================
    // Pipe operations
    // ========================================================================
    {"fn_pipe_map", FPTR(fn_pipe_map)},
    {"fn_pipe_where", FPTR(fn_pipe_where)},
    {"fn_pipe_call", FPTR(fn_pipe_call)},
    {"pipe_map_len", FPTR(pipe_map_len)},
    {"pipe_map_val", FPTR(pipe_map_val)},
    {"pipe_map_key", FPTR(pipe_map_key)},
    {"iter_len", FPTR(iter_len)},
    {"iter_key_at", FPTR(iter_key_at)},
    {"iter_val_at", FPTR(iter_val_at)},

    // ========================================================================
    // Unboxed system functions (native types, no Item boxing overhead)
    // ========================================================================
    {"fn_pow_u", FPTR(fn_pow_u)},
    {"fn_min2_u", FPTR(fn_min2_u)},
    {"fn_max2_u", FPTR(fn_max2_u)},
    {"fn_abs_i", FPTR(fn_abs_i)},
    {"fn_abs_f", FPTR(fn_abs_f)},
    {"fn_neg_i", FPTR(fn_neg_i)},
    {"fn_neg_f", FPTR(fn_neg_f)},
    {"fn_mod_i", FPTR(fn_mod_i)},
    {"fn_idiv_i", FPTR(fn_idiv_i)},
    {"fn_not_u", FPTR(fn_not_u)},
    {"fn_sign_i", FPTR(fn_sign_i)},
    {"fn_sign_f", FPTR(fn_sign_f)},
    {"fn_floor_i", FPTR(fn_floor_i)},
    {"fn_ceil_i", FPTR(fn_ceil_i)},
    {"fn_round_i", FPTR(fn_round_i)},
    // collection length — type-specialized native variants
    {"fn_len_l", FPTR(fn_len_l)},
    {"fn_len_a", FPTR(fn_len_a)},
    {"fn_len_s", FPTR(fn_len_s)},
    {"fn_len_e", FPTR(fn_len_e)},

    // ========================================================================
    // String operations (non-sys-func entries)
    // ========================================================================
    {"fn_strcat", FPTR(fn_strcat)},
    {"fn_normalize", FPTR(fn_normalize)},
    {"fn_substring", FPTR(fn_substring)},
    {"fn_concat", FPTR(fn_concat)},
    {"fn_join", FPTR(fn_join)},

    // ========================================================================
    // MIR swap-safe store functions
    // ========================================================================
    {"_store_i64", FPTR(_store_i64)},
    {"_store_f64", FPTR(_store_f64)},

    // ========================================================================
    // Function creation and calls
    // ========================================================================
    {"to_fn", FPTR(to_fn)},
    {"to_fn_n", FPTR(to_fn_n)},
    {"to_fn_named", FPTR(to_fn_named)},
    {"to_closure", FPTR(to_closure)},
    {"to_closure_named", FPTR(to_closure_named)},
    {"fn_call", FPTR(fn_call)},
    {"fn_call0", FPTR(fn_call0)},
    {"fn_call1", FPTR(fn_call1)},
    {"fn_call2", FPTR(fn_call2)},
    {"fn_call3", FPTR(fn_call3)},

    // ========================================================================
    // Heap allocation
    // ========================================================================
    {"heap_calloc", FPTR(heap_calloc)},
    {"heap_calloc_class", FPTR(heap_calloc_class)},
    {"heap_data_calloc", FPTR(heap_data_calloc)},
    {"heap_create_name", FPTR(heap_create_name)},
    {"heap_create_symbol", FPTR(heap_create_symbol)},
    {"heap_strcpy", FPTR(heap_strcpy)},

    // ========================================================================
    // Type system and pattern matching
    // ========================================================================
    {"base_type", FPTR(base_type)},
    {"const_type", FPTR(const_type)},
    {"const_pattern", FPTR(const_pattern)},
    {"target_equal", FPTR(target_equal)},
    {"fn_query", FPTR(fn_query)},
    {"fn_to", FPTR(fn_to)},

    // ========================================================================
    // Field access / indexing
    // ========================================================================
    {"fn_index", FPTR(fn_index)},
    {"fn_member", FPTR(fn_member)},

    // ========================================================================
    // Path functions
    // ========================================================================
    {"path_new", FPTR(path_new)},
    {"path_extend", FPTR(path_extend)},
    {"path_concat", FPTR(path_concat)},
    {"path_wildcard", FPTR(path_wildcard)},
    {"path_wildcard_recursive", FPTR(path_wildcard_recursive)},
    {"path_resolve_for_iteration", FPTR(path_resolve_for_iteration)},

    // ========================================================================
    // Variadic support
    // ========================================================================
    {"set_vargs", FPTR(set_vargs)},
    {"restore_vargs", FPTR(restore_vargs)},

    // ========================================================================
    // Procedural extras
    // ========================================================================
    {"pn_output_append", FPTR(pn_output_append)},

    // ========================================================================
    // Sort helper
    // ========================================================================
    {"fn_sort_by_keys", FPTR(fn_sort_by_keys)},

    // ========================================================================
    // Array/map mutation (procedural)
    // ========================================================================
    {"fn_array_set", FPTR(fn_array_set)},
    {"fn_map_set", FPTR(fn_map_set)},

    // ========================================================================
    // Bitwise helper
    // ========================================================================
    {"_barg", FPTR(_barg)},

    // ========================================================================
    // VMap functions
    // ========================================================================
    {"vmap_new", FPTR(vmap_new)},
    {"vmap_from_array", FPTR(vmap_from_array)},
    {"vmap_set", FPTR(vmap_set)},

    // ========================================================================
    // Container boxing / error conversion
    // ========================================================================
    {"p2it", FPTR(p2it)},
    {"err2it", FPTR(err2it)},
    {"it2err", FPTR(it2err)},
    // Ret* constructor helpers
    {"ri_ok", FPTR(ri_ok)},
    {"ri_err", FPTR(ri_err)},
    {"rb_ok", FPTR(rb_ok)},
    {"rb_err", FPTR(rb_err)},
    {"ri56_ok", FPTR(ri56_ok)},
    {"ri56_err", FPTR(ri56_err)},
    {"ri64_ok", FPTR(ri64_ok)},
    {"ri64_err", FPTR(ri64_err)},
    {"rf_ok", FPTR(rf_ok)},
    {"rf_err", FPTR(rf_err)},
    {"rs_ok", FPTR(rs_ok)},
    {"rs_err", FPTR(rs_err)},
    {"rsy_ok", FPTR(rsy_ok)},
    {"rsy_err", FPTR(rsy_err)},
    {"rm_ok", FPTR(rm_ok)},
    {"rm_err", FPTR(rm_err)},
    {"rl_ok", FPTR(rl_ok)},
    {"rl_err", FPTR(rl_err)},
    {"re_ok", FPTR(re_ok)},
    {"re_err", FPTR(re_err)},
    {"ro_ok", FPTR(ro_ok)},
    {"ro_err", FPTR(ro_err)},
    {"ra_ok", FPTR(ra_ok)},
    {"ra_err", FPTR(ra_err)},
    {"rr_ok", FPTR(rr_ok)},
    {"rr_err", FPTR(rr_err)},
    {"rp_ok", FPTR(rp_ok)},
    {"rp_err", FPTR(rp_err)},
    {"item_to_ri", FPTR(item_to_ri)},
    {"ri_to_item", FPTR(ri_to_item)},

    // ========================================================================
    // Runtime pool access
    // ========================================================================
    {"get_runtime_pool", FPTR(get_runtime_pool)},

    // ========================================================================
    // Shared runtime context pointer
    // ========================================================================
    {"_lambda_rt", (fn_ptr) &_lambda_rt},

    // ========================================================================
    // StringBuf functions (template literals)
    // ========================================================================
    {"stringbuf_new", FPTR(stringbuf_new)},
    {"stringbuf_append_str", FPTR(stringbuf_append_str)},
    {"stringbuf_append_str_n", FPTR(stringbuf_append_str_n)},
    {"stringbuf_to_string", FPTR(stringbuf_to_string)},

    // ========================================================================
    // JavaScript runtime functions
    // ========================================================================
    {"js_to_number", FPTR(js_to_number)},
    {"js_to_string", FPTR(js_to_string)},
    {"js_to_boolean", FPTR(js_to_boolean)},
    {"js_is_truthy", FPTR(js_is_truthy)},
    {"js_add", FPTR(js_add)},
    {"js_subtract", FPTR(js_subtract)},
    {"js_multiply", FPTR(js_multiply)},
    {"js_divide", FPTR(js_divide)},
    {"js_modulo", FPTR(js_modulo)},
    {"js_power", FPTR(js_power)},
    {"js_equal", FPTR(js_equal)},
    {"js_not_equal", FPTR(js_not_equal)},
    {"js_strict_equal", FPTR(js_strict_equal)},
    {"js_strict_not_equal", FPTR(js_strict_not_equal)},
    {"js_less_than", FPTR(js_less_than)},
    {"js_less_equal", FPTR(js_less_equal)},
    {"js_greater_than", FPTR(js_greater_than)},
    {"js_greater_equal", FPTR(js_greater_equal)},
    {"js_logical_and", FPTR(js_logical_and)},
    {"js_logical_or", FPTR(js_logical_or)},
    {"js_logical_not", FPTR(js_logical_not)},
    {"js_bitwise_and", FPTR(js_bitwise_and)},
    {"js_bitwise_or", FPTR(js_bitwise_or)},
    {"js_bitwise_xor", FPTR(js_bitwise_xor)},
    {"js_bitwise_not", FPTR(js_bitwise_not)},
    {"js_double_to_int32", FPTR(js_double_to_int32)},
    {"js_left_shift", FPTR(js_left_shift)},
    {"js_right_shift", FPTR(js_right_shift)},
    {"js_unsigned_right_shift", FPTR(js_unsigned_right_shift)},
    {"js_unary_plus", FPTR(js_unary_plus)},
    {"js_unary_minus", FPTR(js_unary_minus)},
    {"js_typeof", FPTR(js_typeof)},
    {"js_new_object", FPTR(js_new_object)},
    {"js_property_get", FPTR(js_property_get)},
    {"js_property_set", FPTR(js_property_set)},
    {"js_property_access", FPTR(js_property_access)},
    {"js_array_new", FPTR(js_array_new)},
    {"js_array_new_from_item", FPTR(js_array_new_from_item)},
    {"js_array_get", FPTR(js_array_get)},
    {"js_array_set", FPTR(js_array_set)},
    {"js_array_length", FPTR(js_array_length)},
    {"js_array_push", FPTR(js_array_push)},
    {"js_new_function", FPTR(js_new_function)},
    {"js_new_closure", FPTR(js_new_closure)},
    {"js_alloc_env", FPTR(js_alloc_env)},
    {"js_call_function", FPTR(js_call_function)},
    {"js_apply_function", FPTR(js_apply_function)},
    {"js_constructor_create_object", FPTR(js_constructor_create_object)},
    {"js_debug_check_callee", FPTR(js_debug_check_callee)},
    {"js_get_this", FPTR(js_get_this)},
    {"js_console_log", FPTR(js_console_log)},
    // exception handling
    {"js_throw_value", FPTR(js_throw_value)},
    {"js_check_exception", FPTR(js_check_exception)},
    {"js_clear_exception", FPTR(js_clear_exception)},
    {"js_new_error", FPTR(js_new_error)},
    // method dispatchers
    {"js_string_method", FPTR(js_string_method)},
    {"js_array_method", FPTR(js_array_method)},
    {"js_math_method", FPTR(js_math_method)},
    {"js_math_property", FPTR(js_math_property)},
    {"js_number_method", FPTR(js_number_method)},
    {"js_get_length", FPTR(js_get_length)},
    // DOM API
    {"js_document_method", FPTR(js_document_method)},
    {"js_document_get_property", FPTR(js_document_get_property)},
    {"js_dom_element_method", FPTR(js_dom_element_method)},
    {"js_dom_get_property", FPTR(js_dom_get_property)},
    {"js_dom_wrap_element", FPTR(js_dom_wrap_element)},
    {"js_dom_unwrap_element", FPTR(js_dom_unwrap_element)},
    {"js_is_dom_node", FPTR(js_is_dom_node)},
    {"js_dom_set_property", FPTR(js_dom_set_property)},
    {"js_dom_set_style_property", FPTR(js_dom_set_style_property)},
    {"js_dom_get_style_property", FPTR(js_dom_get_style_property)},
    {"js_get_computed_style", FPTR(js_get_computed_style)},
    // process I/O
    {"js_process_stdout_write", FPTR(js_process_stdout_write)},
    {"js_process_hrtime_bigint", FPTR(js_process_hrtime_bigint)},
    {"js_get_process_argv", FPTR(js_get_process_argv)},
    // global functions
    {"js_parseInt", FPTR(js_parseInt)},
    {"js_parseFloat", FPTR(js_parseFloat)},
    {"js_isNaN", FPTR(js_isNaN)},
    {"js_isFinite", FPTR(js_isFinite)},
    {"js_toFixed", FPTR(js_toFixed)},
    {"js_string_charCodeAt", FPTR(js_string_charCodeAt)},
    {"js_string_fromCharCode", FPTR(js_string_fromCharCode)},
    {"js_array_fill", FPTR(js_array_fill)},
    {"js_array_slice_from", FPTR(js_array_slice_from)},
    {"js_console_log_multi", FPTR(js_console_log_multi)},
    // additional operators
    {"js_instanceof", FPTR(js_instanceof)},
    {"js_in", FPTR(js_in)},
    {"js_nullish_coalesce", FPTR(js_nullish_coalesce)},
    // object utilities
    {"js_object_keys", FPTR(js_object_keys)},
    {"js_object_create", FPTR(js_object_create)},
    {"js_object_define_property", FPTR(js_object_define_property)},
    {"js_array_is_array", FPTR(js_array_is_array)},
    {"js_to_string_val", FPTR(js_to_string_val)},
    {"js_number_property", FPTR(js_number_property)},
    // v9: Object extensions
    {"js_object_values", FPTR(js_object_values)},
    {"js_object_entries", FPTR(js_object_entries)},
    {"js_object_assign", FPTR(js_object_assign)},
    {"js_has_own_property", FPTR(js_has_own_property)},
    {"js_object_freeze", FPTR(js_object_freeze)},
    {"js_object_is_frozen", FPTR(js_object_is_frozen)},
    // v9: Number static methods
    {"js_number_is_integer", FPTR(js_number_is_integer)},
    {"js_number_is_finite", FPTR(js_number_is_finite)},
    {"js_number_is_nan", FPTR(js_number_is_nan)},
    {"js_number_is_safe_integer", FPTR(js_number_is_safe_integer)},
    // v9: Array.from, JSON, delete
    {"js_array_from", FPTR(js_array_from)},
    {"js_json_parse", FPTR(js_json_parse)},
    {"js_json_stringify", FPTR(js_json_stringify)},
    {"js_delete_property", FPTR(js_delete_property)},
    // timing
    {"js_performance_now", FPTR(js_performance_now)},
    {"js_date_now", FPTR(js_date_now)},
    {"js_date_new", FPTR(js_date_new)},
    // shims
    {"js_alert", FPTR(js_alert)},
    // typed arrays
    {"js_typed_array_new", FPTR(js_typed_array_new)},
    {"js_typed_array_get", FPTR(js_typed_array_get)},
    {"js_typed_array_set", FPTR(js_typed_array_set)},
    {"js_typed_array_length", FPTR(js_typed_array_length)},
    {"js_typed_array_fill", FPTR(js_typed_array_fill)},
    {"js_is_typed_array", FPTR(js_is_typed_array)},
    // module variable table
    {"js_set_module_var", FPTR(js_set_module_var)},
    {"js_get_module_var", FPTR(js_get_module_var)},

    // ========================================================================
    // MIR JIT wrappers for RetItem-returning functions
    // ========================================================================
    {"fn_parse1_mir", FPTR(fn_parse1_mir)},
    {"fn_parse2_mir", FPTR(fn_parse2_mir)},
    {"fn_input1_mir", FPTR(fn_input1_mir)},
    {"fn_input2_mir", FPTR(fn_input2_mir)},
    {"pn_cmd1_mir", FPTR(pn_cmd1_mir)},
    {"pn_cmd2_mir", FPTR(pn_cmd2_mir)},
    {"pn_fetch_mir", FPTR(pn_fetch_mir)},
    {"pn_output2_mir", FPTR(pn_output2_mir)},
    {"pn_output3_mir", FPTR(pn_output3_mir)},
    {"pn_io_copy_mir", FPTR(pn_io_copy_mir)},
    {"pn_io_move_mir", FPTR(pn_io_move_mir)},
    {"pn_io_delete_mir", FPTR(pn_io_delete_mir)},
    {"pn_io_mkdir_mir", FPTR(pn_io_mkdir_mir)},
    {"pn_io_touch_mir", FPTR(pn_io_touch_mir)},
    {"pn_io_symlink_mir", FPTR(pn_io_symlink_mir)},
    {"pn_io_chmod_mir", FPTR(pn_io_chmod_mir)},
    {"pn_io_rename_mir", FPTR(pn_io_rename_mir)},
    {"pn_io_fetch1_mir", FPTR(pn_io_fetch1_mir)},
    {"pn_io_fetch2_mir", FPTR(pn_io_fetch2_mir)},
    {"pn_output_append_mir", FPTR(pn_output_append_mir)},

    // ========================================================================
    // Trampolines for calling _b boxed wrappers from MIR Direct (RetItem ABI)
    // ========================================================================
    {"fn_call_boxed_0", FPTR(fn_call_boxed_0)},
    {"fn_call_boxed_1", FPTR(fn_call_boxed_1)},
    {"fn_call_boxed_2", FPTR(fn_call_boxed_2)},
    {"fn_call_boxed_3", FPTR(fn_call_boxed_3)},
    {"fn_call_boxed_4", FPTR(fn_call_boxed_4)},
    {"fn_call_boxed_5", FPTR(fn_call_boxed_5)},
    {"fn_call_boxed_6", FPTR(fn_call_boxed_6)},
    {"fn_call_boxed_7", FPTR(fn_call_boxed_7)},
    {"fn_call_boxed_8", FPTR(fn_call_boxed_8)},
};

const int jit_runtime_import_count = sizeof(jit_runtime_imports) / sizeof(jit_runtime_imports[0]);

#else
// LAMBDA_STATIC build (dylib): provide empty stubs
JitImport jit_runtime_imports[] = {{NULL, NULL}};
const int jit_runtime_import_count = 0;
#endif // !LAMBDA_STATIC
