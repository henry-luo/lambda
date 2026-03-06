// sys_func_registry.cpp — single source of truth for all system function metadata
// Phase 3: Unified Registry (configuration-driven code generation)
//
// This file consolidates:
//   - sys_funcs[] from build_ast.cpp (AST builder metadata)
//   - func_list[] fn_*/pn_* entries from mir.c (JIT import pointers)
//   - native_math_funcs[] from transpile.cpp (native C math optimization)
//   - native_binary_funcs[] from transpile.cpp (native binary func optimization)
//
// Adding a new system function now requires editing ONLY this file.

#include "sys_func_registry.h"  // includes ast.hpp

// External Type globals (defined in lambda-data.cpp)
extern Type TYPE_NULL, TYPE_BOOL, TYPE_INT, TYPE_INT64, TYPE_FLOAT;
extern Type TYPE_STRING, TYPE_SYMBOL, TYPE_DTIME, TYPE_ANY, TYPE_ERROR, TYPE_TYPE;

// ============================================================================
// The Single Source of Truth: sys_func_defs[]
// ============================================================================
// Fields: {id, name, arg_count, return_type, is_proc, is_overloaded,
//          is_method_eligible, first_param_type, can_raise,
//          c_ret_type, c_arg_conv,
//          c_func_name, func_ptr,
//          native_c_name, native_returns_float, native_arg_count}
//
// func_ptr: NULL if function is unimplemented or handled by special transpiler paths.
// native_c_name: NULL if no native C math optimization applies.
// native_arg_count: 0 if native_c_name is NULL.

SysFuncInfo sys_func_defs[] = {
    // ========================================================================
    // Type/conversion functions — all method-eligible
    // ========================================================================
    {SYSFUNC_LEN, "len", 1, &TYPE_INT64, false, false, true, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_ITEM, "fn_len",  NULL, false, 0},

    {SYSFUNC_TYPE, "type", 1, &TYPE_TYPE, false, false, true, LMD_TYPE_ANY, false,
     C_RET_TYPE_PTR, C_ARG_ITEM, "fn_type",  NULL, false, 0},

    {SYSFUNC_NAME, "name", 1, &TYPE_SYMBOL, false, false, true, LMD_TYPE_ANY, false,
     C_RET_SYMBOL, C_ARG_ITEM, "fn_name",  NULL, false, 0},

    {SYSFUNC_INT, "int", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_int",  NULL, false, 0},

    {SYSFUNC_INT64, "int64", 1, &TYPE_INT64, false, false, true, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_ITEM, "fn_int64",  NULL, false, 0},

    {SYSFUNC_FLOAT, "float", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_float",  NULL, false, 0},

    {SYSFUNC_DECIMAL, "decimal", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_decimal",  NULL, false, 0},

    {SYSFUNC_BINARY, "binary", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_binary",  NULL, false, 0},

    {SYSFUNC_NUMBER, "number", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_number", NULL, false, 0},  // unimplemented

    {SYSFUNC_STRING, "string", 1, &TYPE_STRING, false, false, true, LMD_TYPE_ANY, false,
     C_RET_STRING, C_ARG_ITEM, "fn_string",  NULL, false, 0},

    {SYSFUNC_SYMBOL, "symbol", 1, &TYPE_SYMBOL, false, true, true, LMD_TYPE_ANY, false,
     C_RET_SYMBOL, C_ARG_ITEM, "fn_symbol1",  NULL, false, 0},

    {SYSFUNC_SYMBOL2, "symbol", 2, &TYPE_SYMBOL, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_symbol2",  NULL, false, 0},

    // ========================================================================
    // DateTime functions — overloaded with arg count suffix
    // ========================================================================
    {SYSFUNC_DATETIME0, "datetime", 0, &TYPE_DTIME, false, true, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_datetime0",  NULL, false, 0},

    {SYSFUNC_DATETIME, "datetime", 1, &TYPE_DTIME, false, true, true, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_datetime1",  NULL, false, 0},

    {SYSFUNC_DATE0, "date", 0, &TYPE_DTIME, false, true, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_date0",  NULL, false, 0},

    {SYSFUNC_DATE, "date", 1, &TYPE_DTIME, false, true, true, LMD_TYPE_DTIME, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_date1",  NULL, false, 0},

    {SYSFUNC_DATE3, "date", 3, &TYPE_DTIME, false, true, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_date3",  NULL, false, 0},

    {SYSFUNC_TIME0, "time", 0, &TYPE_DTIME, false, true, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_time0",  NULL, false, 0},

    {SYSFUNC_TIME, "time", 1, &TYPE_DTIME, false, true, true, LMD_TYPE_DTIME, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_time1",  NULL, false, 0},

    {SYSFUNC_TIME3, "time", 3, &TYPE_DTIME, false, true, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_time3",  NULL, false, 0},

    {SYSFUNC_JUSTNOW, "justnow", 0, &TYPE_DTIME, false, false, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "fn_justnow",  NULL, false, 0},

    // ========================================================================
    // Collection functions
    // ========================================================================
    {SYSFUNC_SET, "set", -1, &TYPE_ANY, false, false, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_set", NULL, false, 0},  // variadic, unimplemented

    {SYSFUNC_SLICE, "slice", 3, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_slice",  NULL, false, 0},

    {SYSFUNC_ALL, "all", 1, &TYPE_BOOL, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_all", NULL, false, 0},  // unimplemented

    {SYSFUNC_ANY, "any", 1, &TYPE_BOOL, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_any", NULL, false, 0},  // unimplemented

    // min/max — 1-arg is method-eligible, 2-arg is not
    {SYSFUNC_MIN1, "min", 1, &TYPE_ANY, false, true, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_min1",  NULL, false, 0},

    {SYSFUNC_MIN2, "min", 2, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_min2",  "fn_min2_u", true, 2},

    {SYSFUNC_MAX1, "max", 1, &TYPE_ANY, false, true, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_max1",  NULL, false, 0},

    {SYSFUNC_MAX2, "max", 2, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_max2",  "fn_max2_u", true, 2},

    // Aggregation functions — method-eligible on collections
    {SYSFUNC_SUM, "sum", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_sum",  NULL, false, 0},

    {SYSFUNC_AVG, "avg", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_avg",  NULL, false, 0},

    // ========================================================================
    // Math functions — method-eligible on numbers
    // ========================================================================
    {SYSFUNC_ABS, "abs", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_abs",  "fabs", true, 1},

    {SYSFUNC_ROUND, "round", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_round",  "round", true, 1},

    {SYSFUNC_FLOOR, "floor", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_floor",  "floor", true, 1},

    {SYSFUNC_CEIL, "ceil", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_ceil",  "ceil", true, 1},

    // ========================================================================
    // I/O functions — can_raise=true for functions that may fail
    // ========================================================================
    {SYSFUNC_INPUT1, "input", 1, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "fn_input1",  NULL, false, 0},

    {SYSFUNC_INPUT2, "input", 2, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "fn_input2",  NULL, false, 0},

    {SYSFUNC_FORMAT1, "format", 1, &TYPE_STRING, false, true, true, LMD_TYPE_ANY, false,
     C_RET_STRING, C_ARG_ITEM, "fn_format1",  NULL, false, 0},

    {SYSFUNC_FORMAT2, "format", 2, &TYPE_STRING, false, true, true, LMD_TYPE_ANY, false,
     C_RET_STRING, C_ARG_ITEM, "fn_format2",  NULL, false, 0},

    {SYSFUNC_ERROR, "error", 1, &TYPE_ERROR, false, false, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_error",  NULL, false, 0},

    // ========================================================================
    // String functions — method-eligible on strings
    // ========================================================================
    {SYSFUNC_NORMALIZE, "normalize", 1, &TYPE_STRING, false, true, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_normalize1",  NULL, false, 0},

    {SYSFUNC_NORMALIZE2, "normalize", 2, &TYPE_STRING, false, true, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_normalize2",  NULL, false, 0},

    {SYSFUNC_CONTAINS, "contains", 2, &TYPE_BOOL, false, false, true, LMD_TYPE_STRING, false,
     C_RET_BOOL, C_ARG_ITEM, "fn_contains",  NULL, false, 0},

    {SYSFUNC_STARTS_WITH, "starts_with", 2, &TYPE_BOOL, false, false, true, LMD_TYPE_STRING, false,
     C_RET_BOOL, C_ARG_ITEM, "fn_starts_with",  NULL, false, 0},

    {SYSFUNC_ENDS_WITH, "ends_with", 2, &TYPE_BOOL, false, false, true, LMD_TYPE_STRING, false,
     C_RET_BOOL, C_ARG_ITEM, "fn_ends_with",  NULL, false, 0},

    {SYSFUNC_INDEX_OF, "index_of", 2, &TYPE_INT64, false, false, true, LMD_TYPE_STRING, false,
     C_RET_INT64, C_ARG_ITEM, "fn_index_of",  NULL, false, 0},

    {SYSFUNC_LAST_INDEX_OF, "last_index_of", 2, &TYPE_INT64, false, false, true, LMD_TYPE_STRING, false,
     C_RET_INT64, C_ARG_ITEM, "fn_last_index_of",  NULL, false, 0},

    {SYSFUNC_TRIM, "trim", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_trim",  NULL, false, 0},

    {SYSFUNC_TRIM_START, "trim_start", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_trim_start",  NULL, false, 0},

    {SYSFUNC_TRIM_END, "trim_end", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_trim_end",  NULL, false, 0},

    {SYSFUNC_LOWER, "lower", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_lower",  NULL, false, 0},

    {SYSFUNC_UPPER, "upper", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_upper",  NULL, false, 0},

    {SYSFUNC_URL_RESOLVE, "url_resolve", 2, &TYPE_STRING, false, false, false, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_url_resolve",  NULL, false, 0},

    {SYSFUNC_SPLIT, "split", 2, &TYPE_ANY, false, true, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_split2",  NULL, false, 0},

    {SYSFUNC_SPLIT3, "split", 3, &TYPE_ANY, false, true, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_split3",  NULL, false, 0},

    {SYSFUNC_JOIN, "join", 2, &TYPE_ANY, false, true, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_join2",  NULL, false, 0},

    {SYSFUNC_REPLACE, "replace", 3, &TYPE_ANY, false, true, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_replace3",  NULL, false, 0},

    {SYSFUNC_FIND, "find", 2, &TYPE_ANY, false, true, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_find2",  NULL, false, 0},

    {SYSFUNC_FIND3, "find", 3, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_find3",  NULL, false, 0},

    {SYSFUNC_CHARS, "chars", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_chars",  NULL, false, 0},

    {SYSFUNC_ORD, "ord", 1, &TYPE_INT64, false, false, false, LMD_TYPE_STRING, false,
     C_RET_INT64, C_ARG_ITEM, "fn_ord",  NULL, false, 0},

    {SYSFUNC_CHR, "chr", 1, &TYPE_STRING, false, false, false, LMD_TYPE_INT, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_chr",  NULL, false, 0},

    // ========================================================================
    // Vector/array functions — math module
    // ========================================================================
    {SYSFUNC_PROD, "math_prod", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_prod",  NULL, false, 0},

    {SYSFUNC_CUMSUM, "math_cumsum", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_cumsum",  NULL, false, 0},

    {SYSFUNC_CUMPROD, "math_cumprod", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_cumprod",  NULL, false, 0},

    {SYSFUNC_ARGMIN, "argmin", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_argmin",  NULL, false, 0},

    {SYSFUNC_ARGMAX, "argmax", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_argmax",  NULL, false, 0},

    {SYSFUNC_FILL, "fill", 2, &TYPE_ANY, false, false, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_fill",  NULL, false, 0},

    {SYSFUNC_DOT, "math_dot", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_dot",  NULL, false, 0},

    {SYSFUNC_NORM, "math_norm", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_norm",  NULL, false, 0},

    // ========================================================================
    // Statistical functions — math module
    // ========================================================================
    {SYSFUNC_MEAN, "math_mean", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_mean",  NULL, false, 0},

    {SYSFUNC_MEDIAN, "math_median", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_median",  NULL, false, 0},

    {SYSFUNC_VARIANCE, "math_variance", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_variance",  NULL, false, 0},

    {SYSFUNC_DEVIATION, "math_deviation", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_deviation",  NULL, false, 0},

    // ========================================================================
    // Element-wise math functions — math module (with native C math optimization)
    // ========================================================================
    {SYSFUNC_SQRT, "math_sqrt", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_sqrt",  "sqrt", true, 1},

    {SYSFUNC_LOG, "math_log", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_log",  "log", true, 1},

    {SYSFUNC_LOG10, "math_log10", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_log10",  "log10", true, 1},

    {SYSFUNC_EXP, "math_exp", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_exp",  "exp", true, 1},

    {SYSFUNC_SIN, "math_sin", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_sin",  "sin", true, 1},

    {SYSFUNC_COS, "math_cos", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_cos",  "cos", true, 1},

    {SYSFUNC_TAN, "math_tan", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_tan",  "tan", true, 1},

    // inverse trigonometric
    {SYSFUNC_ASIN, "math_asin", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_asin",  "asin", true, 1},

    {SYSFUNC_ACOS, "math_acos", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_acos",  "acos", true, 1},

    {SYSFUNC_ATAN, "math_atan", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_atan",  "atan", true, 1},

    {SYSFUNC_ATAN2, "math_atan2", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_atan2",  "atan2", true, 2},

    // hyperbolic
    {SYSFUNC_SINH, "math_sinh", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_sinh",  "sinh", true, 1},

    {SYSFUNC_COSH, "math_cosh", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_cosh",  "cosh", true, 1},

    {SYSFUNC_TANH, "math_tanh", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_tanh",  "tanh", true, 1},

    // inverse hyperbolic
    {SYSFUNC_ASINH, "math_asinh", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_asinh",  "asinh", true, 1},

    {SYSFUNC_ACOSH, "math_acosh", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_acosh",  "acosh", true, 1},

    {SYSFUNC_ATANH, "math_atanh", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_atanh",  "atanh", true, 1},

    // exponential/logarithmic variants
    {SYSFUNC_EXP2, "math_exp2", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_exp2",  "exp2", true, 1},

    {SYSFUNC_EXPM1, "math_expm1", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_expm1",  "expm1", true, 1},

    {SYSFUNC_LOG2, "math_log2", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_log2",  "log2", true, 1},

    // power/root
    {SYSFUNC_POW_MATH, "math_pow", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_pow",  "fn_pow_u", true, 2},

    {SYSFUNC_CBRT, "math_cbrt", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_cbrt",  "cbrt", true, 1},

    {SYSFUNC_TRUNC, "math_trunc", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_trunc",  "trunc", true, 1},

    {SYSFUNC_HYPOT, "math_hypot", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_hypot",  "hypot", true, 2},

    {SYSFUNC_LOG1P, "math_log1p", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_log1p",  "log1p", true, 1},

    {SYSFUNC_SIGN, "sign", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_sign",  NULL, false, 0},

    // ========================================================================
    // Vector manipulation functions — method-eligible on collections
    // ========================================================================
    {SYSFUNC_REVERSE, "reverse", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_reverse",  NULL, false, 0},

    {SYSFUNC_SORT, "sort", 1, &TYPE_ANY, false, true, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_sort1",  NULL, false, 0},

    {SYSFUNC_SORT2, "sort", 2, &TYPE_ANY, false, true, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_sort2",  NULL, false, 0},

    {SYSFUNC_UNIQUE, "unique", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_unique",  NULL, false, 0},

    // concat() removed from user API — use ++ operator instead
    // {SYSFUNC_CONCAT, "concat", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
    //  C_RET_ITEM, C_ARG_ITEM, "fn_concat",  NULL, false, 0},

    {SYSFUNC_TAKE, "take", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_take",  NULL, false, 0},

    {SYSFUNC_DROP, "drop", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_drop",  NULL, false, 0},

    {SYSFUNC_ZIP, "zip", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_zip",  NULL, false, 0},

    {SYSFUNC_RANGE3, "range", 3, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_range3",  NULL, false, 0},

    {SYSFUNC_QUANTILE, "math_quantile", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_quantile",  NULL, false, 0},

    {SYSFUNC_REDUCE, "reduce", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_reduce",  NULL, false, 0},

    // ========================================================================
    // Parse string functions — overloaded with arg count
    // ========================================================================
    {SYSFUNC_PARSE1, "parse", 1, &TYPE_ANY, false, true, true, LMD_TYPE_STRING, true,
     C_RET_RETITEM, C_ARG_ITEM, "fn_parse1",  NULL, false, 0},

    {SYSFUNC_PARSE2, "parse", 2, &TYPE_ANY, false, true, true, LMD_TYPE_STRING, true,
     C_RET_RETITEM, C_ARG_ITEM, "fn_parse2",  NULL, false, 0},

    // ========================================================================
    // Variadic parameter access — not method-eligible
    // ========================================================================
    {SYSFUNC_VARG0, "varg", 0, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_varg0",  NULL, false, 0},

    {SYSFUNC_VARG1, "varg", 1, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_varg1",  NULL, false, 0},

    // ========================================================================
    // Procedural functions — not method-eligible (side effects)
    // ========================================================================
    {SYSPROC_NOW, "now", 0, &TYPE_DTIME, true, false, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "pn_now", NULL, false, 0},  // unimplemented

    {SYSPROC_TODAY, "today", 0, &TYPE_DTIME, true, false, false, LMD_TYPE_ANY, false,
     C_RET_DTIME, C_ARG_ITEM, "pn_today", NULL, false, 0},  // unimplemented

    {SYSPROC_PRINT, "print", 1, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "pn_print",  NULL, false, 0},

    {SYSPROC_CLOCK, "clock", 0, &TYPE_FLOAT, true, false, false, LMD_TYPE_ANY, false,
     C_RET_DOUBLE, C_ARG_ITEM, "pn_clock",  NULL, false, 0},

    {SYSPROC_FETCH, "fetch", 2, &TYPE_ANY, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_fetch",  NULL, false, 0},

    {SYSPROC_OUTPUT2, "output", 2, &TYPE_ANY, true, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_output2",  NULL, false, 0},

    {SYSPROC_OUTPUT3, "output", 3, &TYPE_ANY, true, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_output3",  NULL, false, 0},

    {SYSPROC_CMD1, "cmd", 1, &TYPE_ANY, true, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_cmd1",  NULL, false, 0},

    {SYSPROC_CMD, "cmd", 2, &TYPE_ANY, true, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_cmd2",  NULL, false, 0},

    // ========================================================================
    // IO module procedures — all can_raise=true for I/O errors
    // ========================================================================
    {SYSPROC_IO_COPY, "io_copy", 2, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_copy",  NULL, false, 0},

    {SYSPROC_IO_MOVE, "io_move", 2, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_move",  NULL, false, 0},

    {SYSPROC_IO_DELETE, "io_delete", 1, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_delete",  NULL, false, 0},

    {SYSPROC_IO_MKDIR, "io_mkdir", 1, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_mkdir",  NULL, false, 0},

    {SYSPROC_IO_TOUCH, "io_touch", 1, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_touch",  NULL, false, 0},

    {SYSPROC_IO_SYMLINK, "io_symlink", 2, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_symlink",  NULL, false, 0},

    {SYSPROC_IO_CHMOD, "io_chmod", 2, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_chmod",  NULL, false, 0},

    {SYSPROC_IO_RENAME, "io_rename", 2, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_rename",  NULL, false, 0},

    {SYSPROC_IO_FETCH, "io_fetch", 1, &TYPE_ANY, true, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_fetch1",  NULL, false, 0},

    {SYSPROC_IO_FETCH, "io_fetch", 2, &TYPE_ANY, true, true, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_fetch2",  NULL, false, 0},

    {SYSFUNC_EXISTS, "exists", 1, &TYPE_BOOL, false, false, false, LMD_TYPE_ANY, false,
     C_RET_BOOL, C_ARG_ITEM, "fn_exists",  NULL, false, 0},

    // ========================================================================
    // Bitwise functions — operate on integers, not method-eligible
    // ========================================================================
    {SYSFUNC_BAND, "band", 2, &TYPE_INT, false, false, false, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_NATIVE, "fn_band",  NULL, false, 0},

    {SYSFUNC_BOR, "bor", 2, &TYPE_INT, false, false, false, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_NATIVE, "fn_bor",  NULL, false, 0},

    {SYSFUNC_BXOR, "bxor", 2, &TYPE_INT, false, false, false, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_NATIVE, "fn_bxor",  NULL, false, 0},

    {SYSFUNC_BNOT, "bnot", 1, &TYPE_INT, false, false, false, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_NATIVE, "fn_bnot",  NULL, false, 0},

    {SYSFUNC_SHL, "shl", 2, &TYPE_INT, false, false, false, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_NATIVE, "fn_shl",  NULL, false, 0},

    {SYSFUNC_SHR, "shr", 2, &TYPE_INT, false, false, false, LMD_TYPE_ANY, false,
     C_RET_INT64, C_ARG_NATIVE, "fn_shr",  NULL, false, 0},

    // ========================================================================
    // VMap functions — handled by special transpiler paths
    // ========================================================================
    {SYSFUNC_VMAP_NEW, "map", 0, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_map0", NULL, false, 0},  // transpiler special case

    {SYSFUNC_VMAP_NEW, "map", 1, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_map1", NULL, false, 0},  // transpiler special case

    {SYSPROC_VMAP_SET, "set", 3, &TYPE_NULL, true, true, true, LMD_TYPE_VMAP, false,
     C_RET_ITEM, C_ARG_ITEM, "pn_set3", NULL, false, 0},  // transpiler special case
};

extern const int sys_func_def_count = sizeof(sys_func_defs) / sizeof(sys_func_defs[0]);
