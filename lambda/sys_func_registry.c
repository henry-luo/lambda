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

// super property access (js_runtime.cpp)
extern Item js_super_property_get(Item receiver, Item key);
extern Item js_super_property_set(Item receiver, Item key, Item value);

// Symbol key check for typed array P9 guard (js_runtime.cpp)
extern int64_t js_key_is_symbol_c(Item key);

// v90: BigInt constructor (js_runtime.cpp)
extern Item js_bigint_constructor(Item value);

// view/edit template apply
extern Item fn_apply1(Item target);
extern Item fn_apply2(Item target, Item options);

// template state store (reactive UI Phase 2)
extern Item tmpl_state_get(Item model_item, const char* template_ref, const char* state_name);
extern void tmpl_state_set(Item model_item, const char* template_ref,
                           const char* state_name, Item value);
extern Item tmpl_state_get_or_init(Item model_item, const char* template_ref,
                                   const char* state_name, Item default_value);

// render map (reactive UI Phase 3 — observer-based reconciliation)
extern void render_map_record(Item source_item, const char* template_ref,
                              Item result_node, Item parent_result, int child_index);
extern void render_map_mark_dirty(Item source_item, const char* template_ref);
extern bool render_map_has_dirty(void);
extern int render_map_retransform(void);
extern Item render_map_get_result(Item source_item, const char* template_ref);

// edit bridge (reactive UI Phase 4 — MarkEditor integration)
extern int edit_bridge_init(void* input_ptr);
extern void edit_bridge_destroy(void);
extern bool edit_bridge_active(void);
extern Item edit_map_update(Item map, const char* key, Item value);
extern Item edit_map_delete(Item map, const char* key);
extern Item edit_elmt_update_attr(Item element, const char* attr_name, Item value);
extern Item edit_elmt_delete_attr(Item element, const char* attr_name);
extern Item edit_elmt_insert_child(Item element, int index, Item child);
extern Item edit_elmt_delete_child(Item element, int index);
extern Item edit_elmt_replace_child(Item element, int index, Item new_child);
extern Item edit_array_set(Item array, int64_t index, Item value);
extern Item edit_array_insert(Item array, int64_t index, Item value);
extern Item edit_array_delete(Item array, int64_t index);
extern Item edit_array_append(Item array, Item value);
extern int edit_commit(const char* description);
extern bool edit_undo(void);
extern bool edit_redo(void);
extern Item edit_current(void);

// edit bridge sys func wrappers
extern Item fn_undo(void);
extern Item fn_redo(void);
extern Item fn_commit0(void);
extern Item fn_commit1(Item description);

// io.http module
extern RetItem pn_io_http_create_server(Item config);
extern RetItem pn_io_http_listen(Item server, Item port);
extern RetItem pn_io_http_route(Item server, Item method, Item path, Item handler);
extern RetItem pn_io_http_use(Item server, Item middleware);
extern RetItem pn_io_http_static(Item server, Item url_path, Item dir_path);
extern RetItem pn_io_http_stop(Item server);

// target_equal is in target.cpp (C++ linkage)
extern bool target_equal(Target* a, Target* b);

// JS runtime functions
#include "js/js_runtime.h"
#include "ts/ts_runtime.h"
#ifdef LAMBDA_PYTHON
#include "py/py_runtime.h"
#include "py/py_class.h"
#include "py/py_bigint.h"
#include "py/py_async.h"
#include "py/py_stdlib.h"
#endif
#ifdef LAMBDA_BASH
#include "bash/bash_runtime.h"
#include "bash/bash_expand.h"
#include "bash/bash_errors.h"
#include "bash/bash_arith.h"
#include "bash/bash_redir.h"
#include "bash/bash_exec.h"
#include "bash/bash_builtins_ext.h"
#include "bash/bash_cond.h"
#include "bash/bash_heredoc.h"
#endif
#ifdef LAMBDA_RUBY
#include "rb/rb_runtime.h"
#endif
#include "js/js_dom.h"
#include "js/js_typed_array.h"
#include "js/js_event_loop.h"
#include "js/js_xhr.h"
extern Item js_buffer_construct(Item arg, Item encoding);

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

    {SYSFUNC_TRUNC, "trunc", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_trunc", FPTR(fn_trunc), "trunc", NPTR(trunc), true, 1},

    {SYSFUNC_SIGN, "sign", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_sign", FPTR(fn_sign), NULL, NULL, false, 0},

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

    {SYSFUNC_HYPOT, "math_hypot", 2, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_hypot", FPTR(fn_math_hypot), "hypot", NPTR(hypot), true, 2},

    {SYSFUNC_LOG1P, "math_log1p", 1, &TYPE_ANY, false, false, true, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_math_log1p", FPTR(fn_math_log1p), "log1p", NPTR(log1p), true, 1},

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

    // io.http module
    {SYSPROC_IO_HTTP_CREATE_SERVER, "io_http_create_server", 1, &TYPE_ANY, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_http_create_server", FPTR(pn_io_http_create_server), NULL, NULL, false, 0},

    {SYSPROC_IO_HTTP_LISTEN, "io_http_listen", 2, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_http_listen", FPTR(pn_io_http_listen), NULL, NULL, false, 0},

    {SYSPROC_IO_HTTP_ROUTE, "io_http_route", 4, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_http_route", FPTR(pn_io_http_route), NULL, NULL, false, 0},

    {SYSPROC_IO_HTTP_USE, "io_http_use", 2, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_http_use", FPTR(pn_io_http_use), NULL, NULL, false, 0},

    {SYSPROC_IO_HTTP_STATIC, "io_http_static", 3, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_http_static", FPTR(pn_io_http_static), NULL, NULL, false, 0},

    {SYSPROC_IO_HTTP_STOP, "io_http_stop", 1, &TYPE_NULL, true, false, false, LMD_TYPE_ANY, true,
     C_RET_RETITEM, C_ARG_ITEM, "pn_io_http_stop", FPTR(pn_io_http_stop), NULL, NULL, false, 0},

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

    // ========================================================================
    // View/edit template apply
    // ========================================================================
    {SYSFUNC_APPLY1, "apply", 1, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_apply1", FPTR(fn_apply1), NULL, NULL, false, 0},

    {SYSFUNC_APPLY2, "apply", 2, &TYPE_ANY, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_apply2", FPTR(fn_apply2), NULL, NULL, false, 0},

    // ========================================================================
    // Edit bridge version control (reactive UI Phase 4)
    // ========================================================================
    {SYSFUNC_EDIT_UNDO, "undo", 0, &TYPE_BOOL, false, false, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_undo", FPTR(fn_undo), NULL, NULL, false, 0},

    {SYSFUNC_EDIT_REDO, "redo", 0, &TYPE_BOOL, false, false, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_redo", FPTR(fn_redo), NULL, NULL, false, 0},

    {SYSFUNC_EDIT_COMMIT, "commit", 0, &TYPE_INT, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_commit0", FPTR(fn_commit0), NULL, NULL, false, 0},

    {SYSFUNC_EDIT_COMMIT1, "commit", 1, &TYPE_INT, false, true, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "fn_commit1", FPTR(fn_commit1), NULL, NULL, false, 0},

    // reactive UI: emit event to parent template handler
    {SYSPROC_EMIT, "emit", 2, &TYPE_ANY, true, false, false, LMD_TYPE_ANY, false,
     C_RET_ITEM, C_ARG_ITEM, "pn_emit", FPTR(pn_emit), NULL, NULL, false, 0},
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
// io.http module
extern Item pn_io_http_create_server_mir(Item config);
extern Item pn_io_http_listen_mir(Item server, Item port);
extern Item pn_io_http_route_mir(Item server, Item method, Item path, Item handler);
extern Item pn_io_http_use_mir(Item server, Item middleware);
extern Item pn_io_http_static_mir(Item server, Item url_path, Item dir_path);
extern Item pn_io_http_stop_mir(Item server);

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

// Debug tracing helpers

// v24: strict mode flag setter (js_runtime.cpp)
extern void js_set_strict_mode(int64_t strict);

// with-statement scope support (js_globals.cpp)
extern void js_with_push(Item obj);
extern void js_with_pop(void);
extern int js_with_save_depth(void);
extern void js_with_restore_depth(int depth);
extern void js_set_global_property(Item key, Item value);

// Object.groupBy / Map.groupBy (ES2024)
extern Item js_object_group_by(Item items, Item callback);
extern Item js_map_group_by(Item items, Item callback);

// Function formal length (ES spec .length)
extern void js_set_formal_length(Item fn_item, int length);

// v25: Reflect API wrappers (js_globals.cpp)
extern Item js_reflect_own_keys(Item obj);
extern Item js_reflect_set(Item obj, Item key, Item value);
extern Item js_reflect_define_property(Item obj, Item key, Item desc);
extern Item js_reflect_delete_property(Item obj, Item key);
extern Item js_reflect_set_prototype_of(Item obj, Item proto);
extern Item js_reflect_prevent_extensions(Item obj);
extern Item js_reflect_apply(Item target, Item this_arg, Item args_array);
extern Item js_get_reflect_object_value();
extern Item js_get_atomics_object_value();

// v23: Performance facade functions (js_runtime.cpp)
extern int64_t js_typeof_is(Item value, const char* type_str);
extern Item js_property_get_str(Item object, const char* key, int key_len);
// v23b: Comparison facades returning raw int64_t 0/1
extern int64_t js_lt_raw(Item left, Item right);
extern int64_t js_gt_raw(Item left, Item right);
extern int64_t js_le_raw(Item left, Item right);
extern int64_t js_ge_raw(Item left, Item right);
extern int64_t js_eq_raw(Item left, Item right);
extern int64_t js_ne_raw(Item left, Item right);
extern int64_t js_loose_eq_raw(Item left, Item right);
extern int64_t js_loose_ne_raw(Item left, Item right);

// debug-only: native test262 harness functions for performance
#ifndef NDEBUG
extern void js_assert_same_value(Item actual, Item expected, Item message);
extern void js_assert_not_same_value(Item actual, Item unexpected, Item message);
extern void js_assert_compare_array(Item actual, Item expected, Item message);
extern void js_assert_deep_equal(Item actual, Item expected, Item message);
extern Item js_compare_array(Item a, Item b);
extern void js_verify_property(Item obj, Item name, Item desc, Item options);
extern void js_assert_throws(Item expected_ctor, Item func, Item message);
extern void js_assert_base(Item must_be_true, Item message);
extern void js_donotevaluate(void);
extern Item js_is_constructor(Item fn);
#endif

JitImport jit_runtime_imports[] = {
    // C library functions
    {"memset", FPTR(memset)},
    {"memcpy", FPTR(memcpy)},
    {"fmod", FPTR(fmod)},
    // float32 bit conversion (C2MIR can't inline these correctly)
    {"f32_to_bits", FPTR(f32_to_bits)},
    {"bits_to_f32", FPTR(bits_to_f32)},
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
    {"array_push_spread_all", FPTR(array_push_spread_all)},
    {"array_end", FPTR(array_end)},
    {"item_spread", FPTR(item_spread)},
    // typed array constructors
    {"array_float_new", FPTR(array_float_new)},
    {"array_float_set", FPTR(array_float_set)},
    {"array_int_new", FPTR(array_int_new)},
    {"array_int_set", FPTR(array_int_set)},
    {"array_num_new", FPTR(array_num_new)},
    {"array_num_set_item", FPTR(array_num_set_item)},

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
    {"map_with_tl", FPTR(map_with_tl)},
    {"map_fill", FPTR(map_fill)},
    {"map_get", FPTR(map_get)},
    {"elmt", FPTR(elmt)},
    {"elmt_with_tl", FPTR(elmt_with_tl)},
    {"elmt_fill", FPTR(elmt_fill)},
    {"elmt_get", FPTR(elmt_get)},
    {"object", FPTR(object)},
    {"object_with_data", FPTR(object_with_data)},
    {"object_with_tl", FPTR(object_with_tl)},
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
    {"ensure_sized_array", FPTR(ensure_sized_array)},

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
    {"js_math_round", FPTR(js_math_round)},
    {"js_math_trunc", FPTR(js_math_trunc)},
    {"js_math_sign", FPTR(js_math_sign)},
    {"js_math_floor", FPTR(js_math_floor)},
    {"js_math_ceil", FPTR(js_math_ceil)},
    {"js_math_ceil_d", FPTR(js_math_ceil_d)},
    {"js_math_round_item", FPTR(js_math_round_item)},
    {"js_math_pow", FPTR(js_math_pow)},
    {"js_math_pow_d", FPTR(js_math_pow_d)},
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
    {"fn_join", FPTR(fn_join)},
    // native String* variants for string functions
    {"fn_starts_with_str", FPTR(fn_starts_with_str)},
    {"fn_ends_with_str", FPTR(fn_ends_with_str)},
    {"fn_ord_str", FPTR(fn_ord_str)},

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
    {"const_type_with_tl", FPTR(const_type_with_tl)},
    {"const_pattern", FPTR(const_pattern)},
    {"const_pattern_with_tl", FPTR(const_pattern_with_tl)},
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
    {"js_to_object", FPTR(js_to_object)},
    {"js_is_truthy", FPTR(js_is_truthy)},
    {"js_is_nullish", FPTR(js_is_nullish)},
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
    {"js_bigint_constructor", FPTR(js_bigint_constructor)},
    {"js_typeof", FPTR(js_typeof)},
    {"js_typeof_is", FPTR(js_typeof_is)},
    {"js_lt_raw", FPTR(js_lt_raw)},
    {"js_gt_raw", FPTR(js_gt_raw)},
    {"js_le_raw", FPTR(js_le_raw)},
    {"js_ge_raw", FPTR(js_ge_raw)},
    {"js_eq_raw", FPTR(js_eq_raw)},
    {"js_ne_raw", FPTR(js_ne_raw)},
    {"js_loose_eq_raw", FPTR(js_loose_eq_raw)},
    {"js_loose_ne_raw", FPTR(js_loose_ne_raw)},
    {"js_new_object", FPTR(js_new_object)},
    {"js_property_get", FPTR(js_property_get)},
    {"js_property_set", FPTR(js_property_set)},
    {"js_property_access", FPTR(js_property_access)},
    {"js_key_is_symbol_c", FPTR(js_key_is_symbol_c)},
    {"js_super_property_get", FPTR(js_super_property_get)},
    {"js_super_property_set", FPTR(js_super_property_set)},
    {"js_property_get_str", FPTR(js_property_get_str)},
    {"js_array_new", FPTR(js_array_new)},
    {"js_array_new_from_item", FPTR(js_array_new_from_item)},
    {"js_build_arguments_object", FPTR(js_build_arguments_object)},
    {"js_set_arguments_info", FPTR(js_set_arguments_info)},
    {"js_create_arguments", FPTR(js_create_arguments)},
    {"js_build_template_object", FPTR(js_build_template_object)},
    {"js_new_check_constructor_return", FPTR(js_new_check_constructor_return)},
    {"js_check_tdz", FPTR(js_check_tdz)},
    {"js_throw_const_assign", FPTR(js_throw_const_assign)},
#ifndef NDEBUG
    {"js_assert_same_value", FPTR(js_assert_same_value)},
    {"js_assert_not_same_value", FPTR(js_assert_not_same_value)},
    {"js_assert_compare_array", FPTR(js_assert_compare_array)},
    {"js_assert_deep_equal", FPTR(js_assert_deep_equal)},
    {"js_compare_array", FPTR(js_compare_array)},
    {"js_verify_property", FPTR(js_verify_property)},
    {"js_assert_throws", FPTR(js_assert_throws)},
    {"js_assert_base", FPTR(js_assert_base)},
    {"js_donotevaluate", FPTR(js_donotevaluate)},
    {"js_is_constructor", FPTR(js_is_constructor)},
#endif
    {"js_array_get", FPTR(js_array_get)},
    {"js_array_set", FPTR(js_array_set)},
    {"js_array_length", FPTR(js_array_length)},
    {"js_array_push", FPTR(js_array_push)},
    {"js_new_function", FPTR(js_new_function)},
    {"js_new_closure", FPTR(js_new_closure)},
    {"js_alloc_env", FPTR(js_alloc_env)},
    {"js_call_function", FPTR(js_call_function)},
    {"js_function_get_ptr", FPTR(js_function_get_ptr)},
    {"js_apply_function", FPTR(js_apply_function)},
    {"js_bind_function", FPTR(js_bind_function)},
    {"js_func_bind", FPTR(js_func_bind)},
    {"js_new_function_from_string", FPTR(js_new_function_from_string)},
    {"js_builtin_eval", FPTR(js_builtin_eval)},
    {"js_create_regex", FPTR(js_create_regex)},
    {"js_regexp_construct", FPTR(js_regexp_construct)},
    {"js_url_construct", FPTR(js_url_construct)},
    {"js_url_construct_with_base", FPTR(js_url_construct_with_base)},
    {"js_url_parse", FPTR(js_url_parse)},
    {"js_url_can_parse", FPTR(js_url_can_parse)},
    {"js_readable_stream_new", FPTR(js_readable_stream_new)},
    {"js_writable_stream_new", FPTR(js_writable_stream_new)},
    {"js_regex_test", FPTR(js_regex_test)},
    {"js_regex_exec", FPTR(js_regex_exec)},
    {"js_constructor_create_object", FPTR(js_constructor_create_object)},
    {"js_new_from_class_object", FPTR(js_new_from_class_object)},
    {"js_new_object_with_shape", FPTR(js_new_object_with_shape)},
    {"js_constructor_create_object_shaped", FPTR(js_constructor_create_object_shaped)},
    {"js_constructor_create_object_shaped_cached", FPTR(js_constructor_create_object_shaped_cached)},
    {"js_get_shaped_slot", FPTR(js_get_shaped_slot)},
    {"js_set_shaped_slot", FPTR(js_set_shaped_slot)},
    {"js_get_slot_f", FPTR(js_get_slot_f)},
    {"js_get_slot_i", FPTR(js_get_slot_i)},
    {"js_set_slot_f", FPTR(js_set_slot_f)},
    {"js_set_slot_i", FPTR(js_set_slot_i)},
    {"js_array_get_int", FPTR(js_array_get_int)},
    {"js_array_set_int", FPTR(js_array_set_int)},
    {"js_debug_check_callee", FPTR(js_debug_check_callee)},
    {"js_get_this", FPTR(js_get_this)},
    {"js_set_this", FPTR(js_set_this)},
    {"js_get_new_target", FPTR(js_get_new_target)},
    {"js_set_new_target", FPTR(js_set_new_target)},
    {"js_set_direct_new_target", FPTR(js_set_direct_new_target)},
    {"js_set_strict_mode", FPTR(js_set_strict_mode)},
    {"js_console_log", FPTR(js_console_log)},
    // exception handling
    {"js_throw_value", FPTR(js_throw_value)},
    {"js_check_exception", FPTR(js_check_exception)},
    {"js_clear_exception", FPTR(js_clear_exception)},
    {"js_require_object_coercible", FPTR(js_require_object_coercible)},
    {"js_throw_syntax_error", FPTR(js_throw_syntax_error)},
    {"js_throw_reference_error", FPTR(js_throw_reference_error)},
    {"js_new_error", FPTR(js_new_error)},
    {"js_new_error_with_name", FPTR(js_new_error_with_name)},
    {"js_new_error_with_stack", FPTR(js_new_error_with_stack)},
    {"js_new_error_with_name_stack", FPTR(js_new_error_with_name_stack)},
    {"js_new_aggregate_error", FPTR(js_new_aggregate_error)},
    {"js_error_set_cause", FPTR(js_error_set_cause)},
    // method dispatchers
    {"js_string_method", FPTR(js_string_method)},
    {"js_array_method", FPTR(js_array_method)},
    {"js_array_method_direct", FPTR(js_array_method_direct)},
    {"js_math_method", FPTR(js_math_method)},
    {"js_math_apply", FPTR(js_math_apply)},
    {"js_method_call_apply", FPTR(js_method_call_apply)},
    {"js_math_property", FPTR(js_math_property)},
    {"js_math_set_property", FPTR(js_math_set_property)},
    {"js_get_math_object_value", FPTR(js_get_math_object_value)},
    {"js_get_json_object_value", FPTR(js_get_json_object_value)},
    {"js_get_console_object_value", FPTR(js_get_console_object_value)},
    {"js_get_reflect_object_value", FPTR(js_get_reflect_object_value)},
    {"js_get_atomics_object_value", FPTR(js_get_atomics_object_value)},
    {"js_get_262_object_value", FPTR(js_get_262_object_value)},
    {"js_get_document_object_value", FPTR(js_get_document_object_value)},
    {"js_is_document_proxy", FPTR(js_is_document_proxy)},
    {"js_document_proxy_method", FPTR(js_document_proxy_method)},
    {"js_document_proxy_get_property", FPTR(js_document_proxy_get_property)},
    {"js_document_proxy_set_property", FPTR(js_document_proxy_set_property)},
    {"js_number_method", FPTR(js_number_method)},
    {"js_get_length", FPTR(js_get_length)},
    {"js_get_length_item", FPTR(js_get_length_item)},
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
    {"js_get_process_object_value", FPTR(js_get_process_object_value)},
    // global functions
    {"js_parseInt", FPTR(js_parseInt)},
    {"js_parseFloat", FPTR(js_parseFloat)},
    {"js_isNaN", FPTR(js_isNaN)},
    {"js_isFinite", FPTR(js_isFinite)},
    {"js_toFixed", FPTR(js_toFixed)},
    {"js_string_charCodeAt", FPTR(js_string_charCodeAt)},
    {"js_string_fromCharCode", FPTR(js_string_fromCharCode)},
    {"js_string_fromCharCode_array", FPTR(js_string_fromCharCode_array)},
    {"js_string_fromCodePoint", FPTR(js_string_fromCodePoint)},
    {"js_string_fromCodePoint_array", FPTR(js_string_fromCodePoint_array)},
    {"js_string_raw", FPTR(js_string_raw)},
    {"js_constructor_static_property", FPTR(js_constructor_static_property)},
    {"js_array_fill", FPTR(js_array_fill)},
    {"js_array_slice_from", FPTR(js_array_slice_from)},
    {"js_console_log_multi", FPTR(js_console_log_multi)},
    // additional operators
    {"js_instanceof", FPTR(js_instanceof)},
    {"js_instanceof_classname", FPTR(js_instanceof_classname)},
    {"js_in", FPTR(js_in)},
    {"js_nullish_coalesce", FPTR(js_nullish_coalesce)},
    // object utilities
    {"js_object_keys", FPTR(js_object_keys)},
    {"js_for_in_keys", FPTR(js_for_in_keys)},
    {"js_object_get_own_property_names", FPTR(js_object_get_own_property_names)},
    {"js_object_get_own_property_symbols", FPTR(js_object_get_own_property_symbols)},
    {"js_object_create", FPTR(js_object_create)},
    {"js_object_define_property", FPTR(js_object_define_property)},
    {"js_object_define_properties", FPTR(js_object_define_properties)},
    {"js_object_get_own_property_descriptor", FPTR(js_object_get_own_property_descriptor)},
    {"js_object_get_own_property_descriptors", FPTR(js_object_get_own_property_descriptors)},
    {"js_set_function_name", FPTR(js_set_function_name)},
    {"js_set_function_source", FPTR(js_set_function_source)},
    {"js_mark_generator_func", FPTR(js_mark_generator_func)},
    {"js_mark_arrow_func", FPTR(js_mark_arrow_func)},
    {"js_set_formal_length", FPTR(js_set_formal_length)},
    {"js_get_constructor", FPTR(js_get_constructor)},
    {"js_get_prototype_of", FPTR(js_get_prototype_of)},
    {"js_reflect_construct", FPTR(js_reflect_construct)},
    {"js_reflect_own_keys", FPTR(js_reflect_own_keys)},
    {"js_reflect_set", FPTR(js_reflect_set)},
    {"js_reflect_define_property", FPTR(js_reflect_define_property)},
    {"js_reflect_delete_property", FPTR(js_reflect_delete_property)},
    {"js_reflect_set_prototype_of", FPTR(js_reflect_set_prototype_of)},
    {"js_reflect_prevent_extensions", FPTR(js_reflect_prevent_extensions)},
    {"js_reflect_apply", FPTR(js_reflect_apply)},
    {"js_make_getter_key", FPTR(js_make_getter_key)},
    {"js_make_setter_key", FPTR(js_make_setter_key)},
    {"js_array_is_array", FPTR(js_array_is_array)},
    {"js_to_string_val", FPTR(js_to_string_val)},
    {"js_number_property", FPTR(js_number_property)},
    // v9: Object extensions
    {"js_object_values", FPTR(js_object_values)},
    {"js_object_entries", FPTR(js_object_entries)},
    {"js_object_from_entries", FPTR(js_object_from_entries)},
    {"js_object_is", FPTR(js_object_is)},
    {"js_object_group_by", FPTR(js_object_group_by)},
    {"js_map_group_by", FPTR(js_map_group_by)},
    {"js_object_assign", FPTR(js_object_assign)},
    {"js_object_spread_into", FPTR(js_object_spread_into)},
    {"js_has_own_property", FPTR(js_has_own_property)},
    {"js_object_freeze", FPTR(js_object_freeze)},
    {"js_object_is_frozen", FPTR(js_object_is_frozen)},
    {"js_object_seal", FPTR(js_object_seal)},
    {"js_object_is_sealed", FPTR(js_object_is_sealed)},
    {"js_object_prevent_extensions", FPTR(js_object_prevent_extensions)},
    {"js_object_is_extensible", FPTR(js_object_is_extensible)},
    // v9: Number static methods
    {"js_number_is_integer", FPTR(js_number_is_integer)},
    {"js_number_is_finite", FPTR(js_number_is_finite)},
    {"js_number_is_nan", FPTR(js_number_is_nan)},
    {"js_number_is_safe_integer", FPTR(js_number_is_safe_integer)},
    // v9: Array.from, JSON, delete
    {"js_array_from", FPTR(js_array_from)},
    {"js_array_from_with_mapper", FPTR(js_array_from_with_mapper)},
    {"js_json_parse", FPTR(js_json_parse)},
    {"js_json_parse_full", FPTR(js_json_parse_full)},
    {"js_json_stringify", FPTR(js_json_stringify)},
    {"js_json_stringify_full", FPTR(js_json_stringify_full)},
    {"js_delete_property", FPTR(js_delete_property)},
    // timing
    {"js_performance_now", FPTR(js_performance_now)},
    {"js_date_now", FPTR(js_date_now)},
    {"js_date_new", FPTR(js_date_new)},
    {"js_date_new_from", FPTR(js_date_new_from)},
    {"js_date_utc", FPTR(js_date_utc)},
    {"js_date_method", FPTR(js_date_method)},
    {"js_date_setter", FPTR(js_date_setter)},
    {"js_date_new_multi", FPTR(js_date_new_multi)},
    {"js_date_parse", FPTR(js_date_parse)},
    {"js_map_collection_new", FPTR(js_map_collection_new)},
    {"js_map_collection_new_from", FPTR(js_map_collection_new_from)},
    {"js_set_collection_new", FPTR(js_set_collection_new)},
    {"js_set_collection_new_from", FPTR(js_set_collection_new_from)},
    {"js_collection_method", FPTR(js_collection_method)},
    {"js_map_method", FPTR(js_map_method)},
    // shims
    {"js_alert", FPTR(js_alert)},
    // typed arrays
    {"js_typed_array_new", FPTR(js_typed_array_new)},
    {"js_typed_array_get", FPTR(js_typed_array_get)},
    {"js_typed_array_set", FPTR(js_typed_array_set)},
    {"js_typed_array_length", FPTR(js_typed_array_length)},
    {"js_typed_array_fill", FPTR(js_typed_array_fill)},
    {"js_is_typed_array", FPTR(js_is_typed_array)},
    {"js_typed_array_construct", FPTR(js_typed_array_construct)},
    {"js_typed_array_new_from_buffer", FPTR(js_typed_array_new_from_buffer)},
    {"js_typed_array_new_from_array", FPTR(js_typed_array_new_from_array)},
    {"js_typed_array_subarray", FPTR(js_typed_array_subarray)},
    {"js_typed_array_slice", FPTR(js_typed_array_slice)},
    {"js_typed_array_set_from", FPTR(js_typed_array_set_from)},
    // ArrayBuffer
    {"js_arraybuffer_new", FPTR(js_arraybuffer_new)},
    {"js_arraybuffer_construct", FPTR(js_arraybuffer_construct)},
    {"js_is_arraybuffer", FPTR(js_is_arraybuffer)},
    {"js_arraybuffer_byte_length", FPTR(js_arraybuffer_byte_length)},
    {"js_arraybuffer_slice", FPTR(js_arraybuffer_slice)},
    {"js_arraybuffer_is_view", FPTR(js_arraybuffer_is_view_item)},
    // Buffer constructor
    {"js_buffer_construct", FPTR(js_buffer_construct)},
    {"js_arraybuffer_wrap", FPTR(js_arraybuffer_wrap)},
    // DataView
    {"js_dataview_new", FPTR(js_dataview_new)},
    {"js_is_dataview", FPTR(js_is_dataview)},
    {"js_dataview_method", FPTR(js_dataview_method)},
    // module variable table
    {"js_set_module_var", FPTR(js_set_module_var)},
    {"js_get_module_var", FPTR(js_get_module_var)},
    // v12: Language features
    {"js_object_rest", FPTR(js_object_rest)},
    {"js_encodeURIComponent", FPTR(js_encodeURIComponent)},
    {"js_decodeURIComponent", FPTR(js_decodeURIComponent)},
    {"js_encodeURI", FPTR(js_encodeURI)},
    {"js_decodeURI", FPTR(js_decodeURI)},
    {"js_unescape", FPTR(js_unescape)},
    {"js_escape", FPTR(js_escape)},
    {"js_atob", FPTR(js_atob)},
    {"js_btoa", FPTR(js_btoa)},
    {"js_native_sha256", FPTR(js_native_sha256)},
    {"js_native_sha384", FPTR(js_native_sha384)},
    {"js_native_sha512", FPTR(js_native_sha512)},
    {"js_get_global_this", FPTR(js_get_global_this)},
    {"js_get_global_object", FPTR(js_get_global_object)},
    {"js_get_global_property", FPTR(js_get_global_property)},
    {"js_get_global_property_strict", FPTR(js_get_global_property_strict)},
    {"js_get_global_builtin_fn", FPTR(js_get_global_builtin_fn)},
    {"js_with_push", FPTR(js_with_push)},
    {"js_with_pop", FPTR(js_with_pop)},
    {"js_with_save_depth", FPTR(js_with_save_depth)},
    {"js_with_restore_depth", FPTR(js_with_restore_depth)},
    {"js_set_global_property", FPTR(js_set_global_property)},

    {"js_symbol_create", FPTR(js_symbol_create)},
    {"js_symbol_for", FPTR(js_symbol_for)},
    {"js_symbol_key_for", FPTR(js_symbol_key_for)},
    {"js_symbol_to_string", FPTR(js_symbol_to_string)},
    {"js_symbol_well_known", FPTR(js_symbol_well_known)},
    // v12: DOM extensions
    {"js_classlist_method", FPTR(js_classlist_method)},
    {"js_classlist_get_property", FPTR(js_classlist_get_property)},
    {"js_dataset_get_property", FPTR(js_dataset_get_property)},
    {"js_dataset_set_property", FPTR(js_dataset_set_property)},
    {"js_location_get_property", FPTR(js_location_get_property)},
    {"js_dom_contains", FPTR(js_dom_contains)},
    // v12b: DOM extensions
    {"js_dom_style_method", FPTR(js_dom_style_method)},
    // v14: Generator runtime
    {"js_generator_create", FPTR(js_generator_create)},
    {"js_generator_next", FPTR(js_generator_next)},
    {"js_generator_return", FPTR(js_generator_return)},
    {"js_generator_throw", FPTR(js_generator_throw)},
    // v15: Generator state machine helper
    {"js_gen_yield_result", FPTR(js_gen_yield_result)},
    {"js_gen_yield_delegate_result", FPTR(js_gen_yield_delegate_result)},
    {"js_iterable_to_array", FPTR(js_iterable_to_array)},
    // v29: Lazy iterator protocol for for-of
    {"js_get_iterator", FPTR(js_get_iterator)},
    {"js_iterator_step", FPTR(js_iterator_step)},
    {"js_iterator_close", FPTR(js_iterator_close)},
    // v14: Promise runtime
    {"js_promise_create", FPTR(js_promise_create)},
    {"js_promise_resolve", FPTR(js_promise_resolve)},
    {"js_promise_reject", FPTR(js_promise_reject)},
    {"js_promise_then", FPTR(js_promise_then)},
    {"js_promise_catch", FPTR(js_promise_catch)},
    {"js_promise_finally", FPTR(js_promise_finally)},
    {"js_promise_all", FPTR(js_promise_all)},
    {"js_promise_race", FPTR(js_promise_race)},
    {"js_promise_any", FPTR(js_promise_any)},
    {"js_promise_all_settled", FPTR(js_promise_all_settled)},
    // v14: Event loop & timers
    {"js_setTimeout", FPTR(js_setTimeout)},
    {"js_setInterval", FPTR(js_setInterval)},
    {"js_clearTimeout", FPTR(js_clearTimeout)},
    {"js_clearInterval", FPTR(js_clearInterval)},
    {"js_setImmediate", FPTR(js_setImmediate)},
    {"js_clearImmediate", FPTR(js_clearImmediate)},
    {"js_structuredClone", FPTR(js_structuredClone)},
    {"js_event_loop_init", FPTR(js_event_loop_init)},
    {"js_event_loop_drain", FPTR(js_event_loop_drain)},
    {"js_microtask_enqueue", FPTR(js_microtask_enqueue)},
    // v30: XMLHttpRequest
    {"js_xhr_new", FPTR(js_xhr_new)},
    // v14: ES Module runtime
    {"js_module_register", FPTR(js_module_register)},
    {"js_module_get", FPTR(js_module_get)},
    {"js_module_namespace_create", FPTR(js_module_namespace_create)},
    // CJS require() support
    {"js_require", FPTR(js_require)},
    // v15: fetch API
    {"js_fetch", FPTR(js_fetch)},
    // Phase 3: Promise.withResolvers
    {"js_promise_with_resolvers", FPTR(js_promise_with_resolvers)},
    // Phase 5: Async/Await sync fast path
    {"js_await_sync", FPTR(js_await_sync)},
    // Phase 6: Async state machine runtime
    {"js_async_must_suspend", FPTR(js_async_must_suspend)},
    {"js_async_get_resolved", FPTR(js_async_get_resolved)},
    {"js_async_context_create", FPTR(js_async_context_create)},
    {"js_async_start", FPTR(js_async_start)},
    {"js_async_get_promise", FPTR(js_async_get_promise)},
    // Phase 3: TextEncoder / TextDecoder
    {"js_text_encoder_new", FPTR(js_text_encoder_new)},
    {"js_text_encoder_encode", FPTR(js_text_encoder_encode)},
    {"js_text_decoder_new", FPTR(js_text_decoder_new)},
    {"js_text_decoder_decode", FPTR(js_text_decoder_decode)},
    // OffscreenCanvas (Canvas text measurement via lib/font)
    {"js_offscreen_canvas_new", FPTR(js_offscreen_canvas_new)},
    // Phase 3: WeakMap / WeakSet (aliased to Map/Set)
    {"js_weakmap_new", FPTR(js_weakmap_new)},
    {"js_weakset_new", FPTR(js_weakset_new)},
    // Proxy
    {"js_proxy_new", FPTR(js_proxy_new)},
    {"js_proxy_revocable", FPTR(js_proxy_revocable)},
    // prototype chain
    {"js_get_prototype", FPTR(js_get_prototype)},
    {"js_set_prototype", FPTR(js_set_prototype)},
    {"js_link_base_prototype", FPTR(js_link_base_prototype)},
    {"js_prototype_lookup", FPTR(js_prototype_lookup)},
    {"js_mark_non_enumerable", FPTR(js_mark_non_enumerable)},
    {"js_mark_non_writable", FPTR(js_mark_non_writable)},
    {"js_func_init_property", FPTR(js_func_init_property)},
    {"js_mark_all_non_enumerable", FPTR(js_mark_all_non_enumerable)},
    {"js_new_number_wrapper", FPTR(js_new_number_wrapper)},
    {"js_new_number_checked", FPTR(js_new_number_checked)},
    {"js_new_boolean_wrapper", FPTR(js_new_boolean_wrapper)},
    {"js_new_string_wrapper", FPTR(js_new_string_wrapper)},

#ifdef LAMBDA_PYTHON
    // ========================================================================
    // Python runtime functions
    // ========================================================================
    // type conversion
    {"py_to_int", FPTR(py_to_int)},
    {"py_to_float", FPTR(py_to_float)},
    {"py_to_str", FPTR(py_to_str)},
    {"py_to_bool", FPTR(py_to_bool)},
    {"py_is_truthy", FPTR(py_is_truthy)},
    // arithmetic
    {"py_add", FPTR(py_add)},
    {"py_subtract", FPTR(py_subtract)},
    {"py_multiply", FPTR(py_multiply)},
    {"py_divide", FPTR(py_divide)},
    {"py_floor_divide", FPTR(py_floor_divide)},
    {"py_modulo", FPTR(py_modulo)},
    {"py_power", FPTR(py_power)},
    {"py_negate", FPTR(py_negate)},
    {"py_positive", FPTR(py_positive)},
    {"py_bit_not", FPTR(py_bit_not)},
    // bitwise
    {"py_bit_and", FPTR(py_bit_and)},
    {"py_bit_or", FPTR(py_bit_or)},
    {"py_bit_xor", FPTR(py_bit_xor)},
    {"py_lshift", FPTR(py_lshift)},
    {"py_rshift", FPTR(py_rshift)},
    // bigint
    {"py_bigint_from_cstr", FPTR(py_bigint_from_cstr)},
    // comparison
    {"py_eq", FPTR(py_eq)},
    {"py_ne", FPTR(py_ne)},
    {"py_lt", FPTR(py_lt)},
    {"py_le", FPTR(py_le)},
    {"py_gt", FPTR(py_gt)},
    {"py_ge", FPTR(py_ge)},
    {"py_is", FPTR(py_is)},
    {"py_is_not", FPTR(py_is_not)},
    {"py_contains", FPTR(py_contains)},
    {"py_match_is_sequence", FPTR(py_match_is_sequence)},
    {"py_match_is_mapping", FPTR(py_match_is_mapping)},
    {"py_match_mapping_rest", FPTR(py_match_mapping_rest)},
    // object/attr
    {"py_getattr", FPTR(py_getattr)},
    {"py_setattr", FPTR(py_setattr)},
    {"py_hasattr", FPTR(py_hasattr)},
    {"py_delattr", FPTR(py_delattr)},
    {"py_new_object", FPTR(py_new_object)},
    // class system
    {"py_class_new", FPTR(py_class_new)},
    {"py_class_new_meta", FPTR(py_class_new_meta)},
    {"py_type_new", FPTR(py_type_new)},
    {"py_new_instance", FPTR(py_new_instance)},
    {"py_bind_method", FPTR(py_bind_method)},
    {"py_is_bound_method", FPTR(py_is_bound_method)},
    {"py_is_class", FPTR(py_is_class)},
    {"py_is_instance", FPTR(py_is_instance)},
    {"py_get_class", FPTR(py_get_class)},
    {"py_mro_lookup", FPTR(py_mro_lookup)},
    {"py_super", FPTR(py_super)},
    {"py_isinstance_v3", FPTR(py_isinstance_v3)},
    {"py_issubclass_v3", FPTR(py_issubclass_v3)},
    // collections
    {"py_list_new", FPTR(py_list_new)},
    {"py_list_append", FPTR(py_list_append)},
    {"py_list_get", FPTR(py_list_get)},
    {"py_list_set", FPTR(py_list_set)},
    {"py_list_length", FPTR(py_list_length)},
    {"py_dict_new", FPTR(py_dict_new)},
    {"py_dict_get", FPTR(py_dict_get)},
    {"py_dict_set", FPTR(py_dict_set)},
    {"py_tuple_new", FPTR(py_tuple_new)},
    {"py_tuple_set", FPTR(py_tuple_set)},
    // subscript/slice
    {"py_subscript_get", FPTR(py_subscript_get)},
    {"py_subscript_set", FPTR(py_subscript_set)},
    {"py_slice_get", FPTR(py_slice_get)},
    {"py_slice_set", FPTR(py_slice_set)},
    {"py_format_value", FPTR(py_format_value)},
    {"py_exception_get_type", FPTR(py_exception_get_type)},
    {"py_builtin_open", FPTR(py_builtin_open)},
    // variadic args
    {"py_build_list_from_args", FPTR(py_build_list_from_args)},
    // iterator
    {"py_get_iterator", FPTR(py_get_iterator)},
    {"py_iterator_next", FPTR(py_iterator_next)},
    {"py_range_new", FPTR(py_range_new)},
    // function/closure
    {"py_new_function", FPTR(py_new_function)},
    {"py_new_closure", FPTR(py_new_closure)},
    {"py_alloc_env", FPTR(py_alloc_env)},
    {"py_set_kwargs_flag", FPTR(py_set_kwargs_flag)},
    {"py_dict_merge", FPTR(py_dict_merge)},
    {"py_call_function", FPTR(py_call_function)},
    {"py_call_function_kw", FPTR(py_call_function_kw)},
    // exception handling
    {"py_raise", FPTR(py_raise)},
    {"py_check_exception", FPTR(py_check_exception)},
    {"py_clear_exception", FPTR(py_clear_exception)},
    {"py_new_exception", FPTR(py_new_exception)},
    // context manager protocol
    {"py_context_enter", FPTR(py_context_enter)},
    {"py_context_exit", FPTR(py_context_exit)},
    {"py_resolve_name_item", FPTR(py_resolve_name_item)},
    // module vars
    {"py_set_module_var", FPTR(py_set_module_var)},
    {"py_get_module_var", FPTR(py_get_module_var)},
    {"py_reset_module_vars", FPTR(py_reset_module_vars)},
    // built-in functions
    {"py_print", FPTR(py_print)},
    {"py_print_ex", FPTR(py_print_ex)},
    {"py_builtin_len", FPTR(py_builtin_len)},
    {"py_builtin_type", FPTR(py_builtin_type)},
    {"py_builtin_isinstance", FPTR(py_builtin_isinstance)},
    {"py_builtin_range", FPTR(py_builtin_range)},
    {"py_builtin_int", FPTR(py_builtin_int)},
    {"py_builtin_float", FPTR(py_builtin_float)},
    {"py_builtin_str", FPTR(py_builtin_str)},
    {"py_builtin_bool", FPTR(py_builtin_bool)},
    {"py_builtin_abs", FPTR(py_builtin_abs)},
    {"py_builtin_min", FPTR(py_builtin_min)},
    {"py_builtin_max", FPTR(py_builtin_max)},
    {"py_builtin_sum", FPTR(py_builtin_sum)},
    {"py_builtin_enumerate", FPTR(py_builtin_enumerate)},
    {"py_builtin_zip", FPTR(py_builtin_zip)},
    {"py_builtin_sorted", FPTR(py_builtin_sorted)},
    {"py_builtin_reversed", FPTR(py_builtin_reversed)},
    {"py_builtin_repr", FPTR(py_builtin_repr)},
    {"py_builtin_hash", FPTR(py_builtin_hash)},
    {"py_builtin_id", FPTR(py_builtin_id)},
    {"py_builtin_input", FPTR(py_builtin_input)},
    {"py_builtin_ord", FPTR(py_builtin_ord)},
    {"py_builtin_chr", FPTR(py_builtin_chr)},
    {"py_builtin_map", FPTR(py_builtin_map)},
    {"py_builtin_filter", FPTR(py_builtin_filter)},
    {"py_builtin_list", FPTR(py_builtin_list)},
    {"py_builtin_dict", FPTR(py_builtin_dict)},
    {"py_builtin_set", FPTR(py_builtin_set)},
    {"py_builtin_tuple", FPTR(py_builtin_tuple)},
    {"py_builtin_round", FPTR(py_builtin_round)},
    {"py_builtin_all", FPTR(py_builtin_all)},
    {"py_builtin_any", FPTR(py_builtin_any)},
    {"py_builtin_bin", FPTR(py_builtin_bin)},
    {"py_builtin_oct", FPTR(py_builtin_oct)},
    {"py_builtin_hex", FPTR(py_builtin_hex)},
    {"py_builtin_divmod", FPTR(py_builtin_divmod)},
    {"py_builtin_pow", FPTR(py_builtin_pow)},
    {"py_builtin_callable", FPTR(py_builtin_callable)},
    {"py_builtin_property", FPTR(py_builtin_property)},
    {"py_property_setter", FPTR(py_property_setter)},
    {"py_property_deleter", FPTR(py_property_deleter)},
    {"py_builtin_sorted_ex", FPTR(py_builtin_sorted_ex)},
    {"py_list_sort_ex", FPTR(py_list_sort_ex)},
    // method dispatchers
    {"py_string_method", FPTR(py_string_method)},
    {"py_list_method", FPTR(py_list_method)},
    {"py_dict_method", FPTR(py_dict_method)},
    // runtime init
    {"py_runtime_set_input", FPTR(py_runtime_set_input)},
    // stop iteration
    {"py_stop_iteration", FPTR(py_stop_iteration)},
    {"py_is_stop_iteration", FPTR(py_is_stop_iteration)},
    // generator protocol
    {"py_gen_create", FPTR(py_gen_create)},
    {"py_gen_get_frame_c", FPTR(py_gen_get_frame_c)},
    {"py_gen_next", FPTR(py_gen_next)},
    {"py_gen_send", FPTR(py_gen_send)},
    // coroutine protocol (Phase D)
    {"py_coro_create", FPTR(py_coro_create)},
    {"py_coro_set_return", FPTR(py_coro_set_return)},
    {"py_coro_get_return", FPTR(py_coro_get_return)},
    {"py_coro_drive", FPTR(py_coro_drive)},
    {"py_asyncio_run", FPTR(py_asyncio_run)},
    {"py_asyncio_sleep", FPTR(py_asyncio_sleep)},
    {"py_asyncio_gather", FPTR(py_asyncio_gather)},
#endif // LAMBDA_PYTHON

#ifdef LAMBDA_BASH
    // ========================================================================
    // Bash runtime functions
    // ========================================================================
    // type conversion
    {"bash_to_int", FPTR(bash_to_int)},
    {"bash_to_int_val", FPTR(bash_to_int_val)},
    {"bash_arith_eval_value", FPTR(bash_arith_eval_value)},
    {"bash_to_string", FPTR(bash_to_string)},
    {"bash_is_truthy", FPTR(bash_is_truthy)},
    {"bash_exit_code", FPTR(bash_exit_code)},
    {"bash_from_exit_code", FPTR(bash_from_exit_code)},
    // arithmetic
    {"bash_add", FPTR(bash_add)},
    {"bash_subtract", FPTR(bash_subtract)},
    {"bash_multiply", FPTR(bash_multiply)},
    {"bash_divide", FPTR(bash_divide)},
    {"bash_modulo", FPTR(bash_modulo)},
    {"bash_power", FPTR(bash_power)},
    {"bash_negate", FPTR(bash_negate)},
    // bitwise
    {"bash_bit_and", FPTR(bash_bit_and)},
    {"bash_bit_or", FPTR(bash_bit_or)},
    {"bash_bit_xor", FPTR(bash_bit_xor)},
    {"bash_bit_not", FPTR(bash_bit_not)},
    {"bash_lshift", FPTR(bash_lshift)},
    {"bash_rshift", FPTR(bash_rshift)},
    // arithmetic comparison
    {"bash_arith_eq", FPTR(bash_arith_eq)},
    {"bash_arith_ne", FPTR(bash_arith_ne)},
    {"bash_arith_lt", FPTR(bash_arith_lt)},
    {"bash_arith_le", FPTR(bash_arith_le)},
    {"bash_arith_gt", FPTR(bash_arith_gt)},
    {"bash_arith_ge", FPTR(bash_arith_ge)},
    {"bash_logical_not", FPTR(bash_logical_not)},
    // runtime arithmetic evaluator
    {"bash_arith_eval_string", FPTR(bash_arith_eval_string)},
    // redirection engine
    {"bash_io_push", FPTR(bash_io_push)},
    {"bash_io_pop", FPTR(bash_io_pop)},
    {"bash_redir_stderr_to_stdout", FPTR(bash_redir_stderr_to_stdout)},
    {"bash_redir_stderr_to_null", FPTR(bash_redir_stderr_to_null)},
    {"bash_redir_stderr_to_file", FPTR(bash_redir_stderr_to_file)},
    {"bash_redir_stderr_restore", FPTR(bash_redir_stderr_restore)},
    // test operators
    {"bash_test_eq", FPTR(bash_test_eq)},
    {"bash_test_ne", FPTR(bash_test_ne)},
    {"bash_test_gt", FPTR(bash_test_gt)},
    {"bash_test_ge", FPTR(bash_test_ge)},
    {"bash_test_lt", FPTR(bash_test_lt)},
    {"bash_test_le", FPTR(bash_test_le)},
    {"bash_test_str_eq", FPTR(bash_test_str_eq)},
    {"bash_test_str_eq_noescape", FPTR(bash_test_str_eq_noescape)},
    {"bash_test_str_eq_literal", FPTR(bash_test_str_eq_literal)},
    {"bash_test_str_ne_literal", FPTR(bash_test_str_ne_literal)},
    {"bash_str_eq", FPTR(bash_str_eq)},
    {"bash_test_str_ne", FPTR(bash_test_str_ne)},
    {"bash_test_str_lt", FPTR(bash_test_str_lt)},
    {"bash_test_str_gt", FPTR(bash_test_str_gt)},
    {"bash_test_z", FPTR(bash_test_z)},
    {"bash_test_n", FPTR(bash_test_n)},
    {"bash_test_f", FPTR(bash_test_f)},
    {"bash_test_d", FPTR(bash_test_d)},
    {"bash_test_e", FPTR(bash_test_e)},
    {"bash_test_r", FPTR(bash_test_r)},
    {"bash_test_w", FPTR(bash_test_w)},
    {"bash_test_x", FPTR(bash_test_x)},
    {"bash_test_s", FPTR(bash_test_s)},
    {"bash_test_l", FPTR(bash_test_l)},
    {"bash_test_regex", FPTR(bash_test_regex)},
    {"bash_test_glob", FPTR(bash_test_glob)},
    {"bash_glob_quote_str", FPTR(bash_glob_quote_str)},
    // string operations
    {"bash_string_length", FPTR(bash_string_length)},
    {"bash_string_concat", FPTR(bash_string_concat)},
    {"bash_var_append", FPTR(bash_var_append)},
    {"bash_string_substring", FPTR(bash_string_substring)},
    {"bash_string_trim_prefix", FPTR(bash_string_trim_prefix)},
    {"bash_string_trim_suffix", FPTR(bash_string_trim_suffix)},
    {"bash_string_replace", FPTR(bash_string_replace)},
    {"bash_string_upper", FPTR(bash_string_upper)},
    {"bash_string_lower", FPTR(bash_string_lower)},
    // parameter expansion
    {"bash_expand_default", FPTR(bash_expand_default)},
    {"bash_expand_assign_default", FPTR(bash_expand_assign_default)},
    {"bash_expand_alt", FPTR(bash_expand_alt)},
    {"bash_expand_error", FPTR(bash_expand_error)},
    {"bash_expand_default_nocolon", FPTR(bash_expand_default_nocolon)},
    {"bash_expand_assign_default_nocolon", FPTR(bash_expand_assign_default_nocolon)},
    {"bash_expand_alt_nocolon", FPTR(bash_expand_alt_nocolon)},
    {"bash_expand_error_nocolon", FPTR(bash_expand_error_nocolon)},
    {"bash_expand_trim_prefix", FPTR(bash_expand_trim_prefix)},
    {"bash_expand_trim_prefix_long", FPTR(bash_expand_trim_prefix_long)},
    {"bash_expand_trim_suffix", FPTR(bash_expand_trim_suffix)},
    {"bash_expand_trim_suffix_long", FPTR(bash_expand_trim_suffix_long)},
    {"bash_expand_replace", FPTR(bash_expand_replace)},
    {"bash_expand_replace_all", FPTR(bash_expand_replace_all)},
    {"bash_expand_replace_prefix", FPTR(bash_expand_replace_prefix)},
    {"bash_expand_replace_suffix", FPTR(bash_expand_replace_suffix)},
    {"bash_expand_substring", FPTR(bash_expand_substring)},
    {"bash_expand_upper_first", FPTR(bash_expand_upper_first)},
    {"bash_expand_upper_all", FPTR(bash_expand_upper_all)},
    {"bash_expand_lower_first", FPTR(bash_expand_lower_first)},
    {"bash_expand_lower_all", FPTR(bash_expand_lower_all)},
    {"bash_expand_toggle_first", FPTR(bash_expand_toggle_first)},
    {"bash_expand_toggle_all", FPTR(bash_expand_toggle_all)},
    {"bash_array_casemod", FPTR(bash_array_casemod)},
    {"bash_expand_indirect", FPTR(bash_expand_indirect)},
    {"bash_expand_prefix_names", FPTR(bash_expand_prefix_names)},
    {"bash_procsub_in", FPTR(bash_procsub_in)},
    {"bash_procsub_out", FPTR(bash_procsub_out)},
    {"bash_procsub_wait_all", FPTR(bash_procsub_wait_all)},
    // array operations
    {"bash_int_to_item", FPTR(bash_int_to_item)},
    {"bash_array_new", FPTR(bash_array_new)},
    {"bash_ensure_array", FPTR(bash_ensure_array)},
    {"bash_array_set", FPTR(bash_array_set)},
    {"bash_array_get", FPTR(bash_array_get)},
    {"bash_array_append", FPTR(bash_array_append)},
    {"bash_array_concat", FPTR(bash_array_concat)},
    {"bash_array_concat_positional", FPTR(bash_array_concat_positional)},
    {"bash_array_elem_append", FPTR(bash_array_elem_append)},
    {"bash_array_length", FPTR(bash_array_length)},
    {"bash_array_count", FPTR(bash_array_count)},
    {"bash_array_all", FPTR(bash_array_all)},
    {"bash_array_unset", FPTR(bash_array_unset)},
    {"bash_array_slice", FPTR(bash_array_slice)},
    // associative array operations
    {"bash_assoc_new", FPTR(bash_assoc_new)},
    {"bash_ensure_assoc", FPTR(bash_ensure_assoc)},
    {"bash_assoc_set", FPTR(bash_assoc_set)},
    {"bash_assoc_init_word", FPTR(bash_assoc_init_word)},
    {"bash_array_init_word", FPTR(bash_array_init_word)},
    {"bash_assoc_get", FPTR(bash_assoc_get)},
    {"bash_assoc_keys", FPTR(bash_assoc_keys)},
    {"bash_assoc_values", FPTR(bash_assoc_values)},
    {"bash_assoc_unset", FPTR(bash_assoc_unset)},
    {"bash_assoc_length", FPTR(bash_assoc_length)},
    {"bash_assoc_count", FPTR(bash_assoc_count)},
    // variable attributes
    {"bash_declare_var", FPTR(bash_declare_var)},
    {"bash_declare_local_var", FPTR(bash_declare_local_var)},
    {"bash_declare_nameref", FPTR(bash_declare_nameref)},
    {"bash_declare_local_nameref", FPTR(bash_declare_local_nameref)},
    {"bash_get_var_attrs", FPTR(bash_get_var_attrs)},
    {"bash_is_assoc", FPTR(bash_is_assoc)},
    {"bash_declare_print_var", FPTR(bash_declare_print_var)},
    {"bash_builtin_set_dump", FPTR(bash_builtin_set_dump)},
    // variable scope
    {"bash_set_var", FPTR(bash_set_var)},
    {"bash_restore_var_if_not_posix", FPTR(bash_restore_var_if_not_posix)},
    {"bash_set_cmd_env_var", FPTR(bash_set_cmd_env_var)},
    {"bash_restore_cmd_env_var", FPTR(bash_restore_cmd_env_var)},
    {"bash_get_var", FPTR(bash_get_var)},
    {"bash_set_local_var", FPTR(bash_set_local_var)},
    {"bash_export_var", FPTR(bash_export_var)},
    {"bash_unset_var", FPTR(bash_unset_var)},
    {"bash_set_positional", FPTR(bash_set_positional)},
    {"bash_push_positional", FPTR(bash_push_positional)},
    {"bash_pop_positional", FPTR(bash_pop_positional)},
    {"bash_get_positional", FPTR(bash_get_positional)},
    {"bash_get_arg_count", FPTR(bash_get_arg_count)},
    {"bash_get_all_args", FPTR(bash_get_all_args)},
    {"bash_get_all_args_string", FPTR(bash_get_all_args_string)},
    {"bash_get_positional_array", FPTR(bash_get_positional_array)},
    {"bash_positional_slice", FPTR(bash_positional_slice)},
    {"bash_positional_slice_array", FPTR(bash_positional_slice_array)},
    {"bash_shift_args", FPTR(bash_shift_args)},
    {"bash_arg_builder_start", FPTR(bash_arg_builder_start)},
    {"bash_arg_builder_push", FPTR(bash_arg_builder_push)},
    {"bash_arg_builder_push_at", FPTR(bash_arg_builder_push_at)},
    {"bash_arg_builder_push_default_at", FPTR(bash_arg_builder_push_default_at)},
    {"bash_arg_builder_push_default_star", FPTR(bash_arg_builder_push_default_star)},
    {"bash_arg_builder_push_array", FPTR(bash_arg_builder_push_array)},
    {"bash_arg_builder_get_ptr", FPTR(bash_arg_builder_get_ptr)},
    {"bash_arg_builder_get_count", FPTR(bash_arg_builder_get_count)},
    {"bash_get_exit_code", FPTR(bash_get_exit_code)},
    {"bash_set_exit_code", FPTR(bash_set_exit_code)},
    {"bash_save_exit_code", FPTR(bash_save_exit_code)},
    {"bash_restore_exit_code", FPTR(bash_restore_exit_code)},
    {"bash_negate_exit_code", FPTR(bash_negate_exit_code)},
    {"bash_return_with_code", FPTR(bash_return_with_code)},
    {"bash_get_script_name", FPTR(bash_get_script_name)},
    {"bash_set_script_name", FPTR(bash_set_script_name)},
    {"bash_get_pid", FPTR(bash_get_pid)},
    {"bash_get_last_bg_pid", FPTR(bash_get_last_bg_pid)},
    {"bash_get_shell_flags", FPTR(bash_get_shell_flags)},
    {"bash_get_lineno", FPTR(bash_get_lineno)},
    {"bash_set_lineno", FPTR(bash_set_lineno)},
    {"bash_set_command", FPTR(bash_set_command)},
    {"bash_set_arith_context", FPTR(bash_set_arith_context)},
    {"bash_get_funcname", FPTR(bash_get_funcname)},
    {"bash_get_funcname_count", FPTR(bash_get_funcname_count)},
    {"bash_get_bash_source", FPTR(bash_get_bash_source)},
    {"bash_get_bash_lineno", FPTR(bash_get_bash_lineno)},
    {"bash_get_bash_source_count", FPTR(bash_get_bash_source_count)},
    {"bash_get_bash_lineno_count", FPTR(bash_get_bash_lineno_count)},
    {"bash_push_funcname", FPTR(bash_push_funcname)},
    {"bash_pop_funcname", FPTR(bash_pop_funcname)},
    {"bash_get_funcname_all", FPTR(bash_get_funcname_all)},
    {"bash_push_argv_frame", FPTR(bash_push_argv_frame)},
    {"bash_pop_bash_argv", FPTR(bash_pop_bash_argv)},
    {"bash_get_bash_argv", FPTR(bash_get_bash_argv)},
    {"bash_get_bash_argv_count", FPTR(bash_get_bash_argv_count)},
    {"bash_get_bash_argv_all", FPTR(bash_get_bash_argv_all)},
    {"bash_get_bash_argc", FPTR(bash_get_bash_argc)},
    {"bash_get_bash_argc_count", FPTR(bash_get_bash_argc_count)},
    {"bash_push_source", FPTR(bash_push_source)},
    {"bash_pop_source", FPTR(bash_pop_source)},
    {"bash_push_call_frame", FPTR(bash_push_call_frame)},
    {"bash_pop_call_frame", FPTR(bash_pop_call_frame)},
    // output capture
    {"bash_begin_capture", FPTR(bash_begin_capture)},
    {"bash_end_capture", FPTR(bash_end_capture)},
    {"bash_end_capture_raw", FPTR(bash_end_capture_raw)},
    {"bash_cmd_sub_word_split", FPTR(bash_cmd_sub_word_split)},
    {"bash_cmd_sub_enter", FPTR(bash_cmd_sub_enter)},
    {"bash_cmd_sub_exit", FPTR(bash_cmd_sub_exit)},
    {"bash_raw_write", FPTR(bash_raw_write)},
    {"bash_write_heredoc", FPTR(bash_write_heredoc)},
    {"bash_write_stderr", FPTR(bash_write_stderr)},
    {"bash_raw_putc", FPTR(bash_raw_putc)},
    // error formatting (Module 4)
    {"bash_errmsg", FPTR(bash_errmsg)},
    {"bash_errmsg_at", FPTR(bash_errmsg_at)},
    {"bash_err_readonly", FPTR(bash_err_readonly)},
    {"bash_err_bad_substitution", FPTR(bash_err_bad_substitution)},
    {"bash_err_unbound_variable", FPTR(bash_err_unbound_variable)},
    {"bash_err_not_found", FPTR(bash_err_not_found)},
    {"bash_err_syntax", FPTR(bash_err_syntax)},
    {"bash_err_numeric_arg", FPTR(bash_err_numeric_arg)},
    {"bash_err_invalid_option", FPTR(bash_err_invalid_option)},
    {"bash_err_too_many_args", FPTR(bash_err_too_many_args)},
    {"bash_err_not_valid_identifier", FPTR(bash_err_not_valid_identifier)},
    {"bash_err_ambiguous_redirect", FPTR(bash_err_ambiguous_redirect)},
    {"bash_err_division_by_zero", FPTR(bash_err_division_by_zero)},
    {"bash_err_unset_readonly", FPTR(bash_err_unset_readonly)},
    {"bash_err_circular_nameref", FPTR(bash_err_circular_nameref)},
    {"bash_err_declare_not_found", FPTR(bash_err_declare_not_found)},
    {"bash_err_no_such_file", FPTR(bash_err_no_such_file)},
    {"bash_err_param_not_set", FPTR(bash_err_param_not_set)},
    // pipeline stdin item passing
    {"bash_set_stdin_item", FPTR(bash_set_stdin_item)},
    {"bash_get_stdin_item", FPTR(bash_get_stdin_item)},
    {"bash_clear_stdin_item", FPTR(bash_clear_stdin_item)},
    {"bash_stdin_item_is_set", FPTR(bash_stdin_item_is_set)},
    // file redirections
    {"bash_redirect_write", FPTR(bash_redirect_write)},
    {"bash_redirect_append", FPTR(bash_redirect_append)},
    {"bash_redirect_read", FPTR(bash_redirect_read)},
    // external command execution
    {"bash_exec_external", FPTR(bash_exec_external)},
    {"bash_exec_cmd_with_array", FPTR(bash_exec_cmd_with_array)},
    // expansions (tilde, glob, brace)
    {"bash_expand_tilde", FPTR(bash_expand_tilde)},
    {"bash_expand_tilde_assign", FPTR(bash_expand_tilde_assign)},
    {"bash_expand_tilde_assign_arg", FPTR(bash_expand_tilde_assign_arg)},
    {"bash_glob_expand", FPTR(bash_glob_expand)},
    {"bash_expand_brace", FPTR(bash_expand_brace)},
    {"bash_words_split_into", FPTR(bash_words_split_into)},
    {"bash_ifs_split_into", FPTR(bash_ifs_split_into)},
    {"bash_ifs_split_default_at", FPTR(bash_ifs_split_default_at)},
    {"bash_ifs_split_default_star", FPTR(bash_ifs_split_default_star)},
    // word expansion (Module 1)
    {"bash_word_split", FPTR(bash_word_split)},
    {"bash_word_split_into", FPTR(bash_word_split_into)},
    {"bash_quote_remove", FPTR(bash_quote_remove)},
    {"bash_process_ansi_escapes", FPTR(bash_process_ansi_escapes)},
    {"bash_expand_word", FPTR(bash_expand_word)},
    {"bash_set_positional_from_array", FPTR(bash_set_positional_from_array)},
    // scope lifecycle
    {"bash_scope_push", FPTR(bash_scope_push)},
    {"bash_scope_pop", FPTR(bash_scope_pop)},
    {"bash_scope_push_subshell", FPTR(bash_scope_push_subshell)},
    {"bash_scope_pop_subshell", FPTR(bash_scope_pop_subshell)},
    {"bash_reset_errexit", FPTR(bash_reset_errexit)},
    {"bash_comsub_reset_errexit", FPTR(bash_comsub_reset_errexit)},
    // built-in commands
    {"bash_builtin_echo", FPTR(bash_builtin_echo)},
    {"bash_builtin_printf", FPTR(bash_builtin_printf)},
    {"bash_process_escapes", FPTR(bash_process_escapes)},
    {"bash_builtin_let", FPTR(bash_builtin_let)},
    {"bash_builtin_type", FPTR(bash_builtin_type)},
    {"bash_builtin_command", FPTR(bash_builtin_command)},
    {"bash_builtin_test", FPTR(bash_builtin_test)},
    {"bash_builtin_true", FPTR(bash_builtin_true)},
    {"bash_builtin_false", FPTR(bash_builtin_false)},
    {"bash_builtin_exit", FPTR(bash_builtin_exit)},
    {"bash_builtin_return", FPTR(bash_builtin_return)},
    {"bash_builtin_read", FPTR(bash_builtin_read)},
    {"bash_builtin_shift", FPTR(bash_builtin_shift)},
    {"bash_builtin_local", FPTR(bash_builtin_local)},
    {"bash_builtin_export", FPTR(bash_builtin_export)},
    {"bash_builtin_unset", FPTR(bash_builtin_unset)},
    {"bash_builtin_cd", FPTR(bash_builtin_cd)},
    {"bash_builtin_pwd", FPTR(bash_builtin_pwd)},
    {"bash_builtin_pushd", FPTR(bash_builtin_pushd)},
    {"bash_builtin_popd", FPTR(bash_builtin_popd)},
    {"bash_builtin_dirs", FPTR(bash_builtin_dirs)},
    {"bash_builtin_getopts", FPTR(bash_builtin_getopts)},
    {"bash_dirstack_get", FPTR(bash_dirstack_get)},
    {"bash_dirstack_total", FPTR(bash_dirstack_total)},
    {"bash_builtin_caller", FPTR(bash_builtin_caller)},
    // pipeline builtins
    {"bash_builtin_cat", FPTR(bash_builtin_cat)},
    {"bash_builtin_wc", FPTR(bash_builtin_wc)},
    {"bash_builtin_head", FPTR(bash_builtin_head)},
    {"bash_builtin_tail", FPTR(bash_builtin_tail)},
    {"bash_builtin_grep", FPTR(bash_builtin_grep)},
    {"bash_builtin_sort", FPTR(bash_builtin_sort)},
    {"bash_builtin_tr", FPTR(bash_builtin_tr)},
    {"bash_builtin_cut", FPTR(bash_builtin_cut)},
    // runtime init/cleanup
    {"bash_runtime_init", FPTR(bash_runtime_init)},
    {"bash_runtime_cleanup", FPTR(bash_runtime_cleanup)},
    // environment variable integration
    {"bash_env_import", FPTR(bash_env_import)},
    {"bash_env_sync_export", FPTR(bash_env_sync_export)},
    // script sourcing
    {"bash_source_file", FPTR(bash_source_file)},
    // runtime function registry
    {"bash_register_rt_func", FPTR(bash_register_rt_func)},
    {"bash_register_rt_func_with_source", FPTR(bash_register_rt_func_with_source)},
    {"bash_print_all_functions", FPTR(bash_print_all_functions)},
    {"bash_declare_print_func", FPTR(bash_declare_print_func)},
    {"bash_call_rt_func", FPTR(bash_call_rt_func)},
    {"bash_lookup_rt_func", FPTR(bash_lookup_rt_func)},
    // shell options
    {"bash_set_option", FPTR(bash_set_option)},
    {"bash_get_option_errexit", FPTR(bash_get_option_errexit)},
    {"bash_get_option_nounset", FPTR(bash_get_option_nounset)},
    {"bash_get_option_xtrace", FPTR(bash_get_option_xtrace)},
    {"bash_get_option_pipefail", FPTR(bash_get_option_pipefail)},
    {"bash_errexit_push", FPTR(bash_errexit_push)},
    {"bash_errexit_pop", FPTR(bash_errexit_pop)},
    {"bash_set_errexit_suppressed", FPTR(bash_set_errexit_suppressed)},
    {"bash_check_errexit", FPTR(bash_check_errexit)},
    // signal handling / trap (Phase 8)
    {"bash_trap_set", FPTR(bash_trap_set)},
    {"bash_trap_run_exit", FPTR(bash_trap_run_exit)},
    {"bash_trap_check", FPTR(bash_trap_check)},
    {"bash_run_debug_trap", FPTR(bash_run_debug_trap)},
    {"bash_run_return_trap", FPTR(bash_run_return_trap)},
    {"bash_eval_string", FPTR(bash_eval_string)},
    // exec engine (Module 8)
    {"bash_exec_builtin", FPTR(bash_exec_builtin)},
    {"bash_exec_redir_open", FPTR(bash_exec_redir_open)},
    {"bash_exec_redir_dup", FPTR(bash_exec_redir_dup)},
    {"bash_exec_redir_close", FPTR(bash_exec_redir_close)},
    {"bash_exec_redir_varfd", FPTR(bash_exec_redir_varfd)},
    {"bash_exec_subscript", FPTR(bash_exec_subscript)},
    {"bash_exec_init", FPTR(bash_exec_init)},
    {"bash_exec_cleanup", FPTR(bash_exec_cleanup)},
    // extended builtins (Module 9)
    {"bash_builtin_mapfile", FPTR(bash_builtin_mapfile)},
    {"bash_builtin_wait", FPTR(bash_builtin_wait)},
    {"bash_builtin_hash", FPTR(bash_builtin_hash)},
    {"bash_builtin_enable", FPTR(bash_builtin_enable)},
    {"bash_builtin_compgen", FPTR(bash_builtin_compgen)},
    {"bash_builtin_builtin", FPTR(bash_builtin_builtin)},
    {"bash_builtin_umask", FPTR(bash_builtin_umask)},
    {"bash_trap_print_all", FPTR(bash_trap_print_all)},
    {"bash_trap_print_one", FPTR(bash_trap_print_one)},
    // conditional engine (Module 10)
    {"bash_cond_regex", FPTR(bash_cond_regex)},
    {"bash_get_rematch", FPTR(bash_get_rematch)},
    {"bash_get_rematch_count", FPTR(bash_get_rematch_count)},
    {"bash_get_rematch_all", FPTR(bash_get_rematch_all)},
    {"bash_clear_rematch", FPTR(bash_clear_rematch)},
    // PIPESTATUS array
    {"bash_pipestatus_reset", FPTR(bash_pipestatus_reset)},
    {"bash_pipestatus_set", FPTR(bash_pipestatus_set)},
    {"bash_pipestatus_apply_simple", FPTR(bash_pipestatus_apply_simple)},
    {"bash_pipestatus_apply_pipefail", FPTR(bash_pipestatus_apply_pipefail)},
    {"bash_get_pipestatus", FPTR(bash_get_pipestatus)},
    {"bash_get_pipestatus_all", FPTR(bash_get_pipestatus_all)},
    {"bash_get_pipestatus_count_item", FPTR(bash_get_pipestatus_count_item)},
    {"bash_cond_pattern", FPTR(bash_cond_pattern)},
    {"bash_test_nt", FPTR(bash_test_nt)},
    {"bash_test_ot", FPTR(bash_test_ot)},
    {"bash_test_ef", FPTR(bash_test_ef)},
    {"bash_get_option_nocasematch", FPTR(bash_get_option_nocasematch)},
    {"bash_get_option_extglob", FPTR(bash_get_option_extglob)},
    // heredoc engine (Module 11)
    {"bash_heredoc_expand", FPTR(bash_heredoc_expand)},
    {"bash_herestring_expand", FPTR(bash_herestring_expand)},
    {"bash_append_newline", FPTR(bash_append_newline)},
    {"bash_heredoc_strip_tabs", FPTR(bash_heredoc_strip_tabs)},
    {"bash_set_heredoc_stdin", FPTR(bash_set_heredoc_stdin)},
    {"bash_get_heredoc_stdin", FPTR(bash_get_heredoc_stdin)},
    {"bash_clear_heredoc_stdin", FPTR(bash_clear_heredoc_stdin)},
#endif // LAMBDA_BASH

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
    // io.http module
    {"pn_io_http_create_server_mir", FPTR(pn_io_http_create_server_mir)},
    {"pn_io_http_listen_mir", FPTR(pn_io_http_listen_mir)},
    {"pn_io_http_route_mir", FPTR(pn_io_http_route_mir)},
    {"pn_io_http_use_mir", FPTR(pn_io_http_use_mir)},
    {"pn_io_http_static_mir", FPTR(pn_io_http_static_mir)},
    {"pn_io_http_stop_mir", FPTR(pn_io_http_stop_mir)},

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

    // ========================================================================
    // Template state store (reactive UI Phase 2)
    // ========================================================================
    {"tmpl_state_get", FPTR(tmpl_state_get)},
    {"tmpl_state_set", FPTR(tmpl_state_set)},
    {"tmpl_state_get_or_init", FPTR(tmpl_state_get_or_init)},

    // ========================================================================
    // Render map (reactive UI Phase 3 — observer-based reconciliation)
    // ========================================================================
    {"render_map_record", FPTR(render_map_record)},
    {"render_map_mark_dirty", FPTR(render_map_mark_dirty)},
    {"render_map_has_dirty", FPTR(render_map_has_dirty)},
    {"render_map_retransform", FPTR(render_map_retransform)},
    {"render_map_get_result", FPTR(render_map_get_result)},

    // ========================================================================
    // Edit bridge (reactive UI Phase 4 — MarkEditor integration)
    // ========================================================================
    {"edit_bridge_init", FPTR(edit_bridge_init)},
    {"edit_bridge_destroy", FPTR(edit_bridge_destroy)},
    {"edit_bridge_active", FPTR(edit_bridge_active)},
    {"edit_map_update", FPTR(edit_map_update)},
    {"edit_map_delete", FPTR(edit_map_delete)},
    {"edit_elmt_update_attr", FPTR(edit_elmt_update_attr)},
    {"edit_elmt_delete_attr", FPTR(edit_elmt_delete_attr)},
    {"edit_elmt_insert_child", FPTR(edit_elmt_insert_child)},
    {"edit_elmt_delete_child", FPTR(edit_elmt_delete_child)},
    {"edit_elmt_replace_child", FPTR(edit_elmt_replace_child)},
    {"edit_array_set", FPTR(edit_array_set)},
    {"edit_array_insert", FPTR(edit_array_insert)},
    {"edit_array_delete", FPTR(edit_array_delete)},
    {"edit_array_append", FPTR(edit_array_append)},
    {"edit_commit", FPTR(edit_commit)},
    {"edit_undo", FPTR(edit_undo)},
    {"edit_redo", FPTR(edit_redo)},
    {"edit_current", FPTR(edit_current)},

    // ========================================================================
    // TS runtime
    // ========================================================================
    {"ts_typeof", FPTR(ts_typeof)},
    {"ts_check_shape", FPTR(ts_check_shape)},
    {"ts_assert_type", FPTR(ts_assert_type)},
    {"ts_type_info", FPTR(ts_type_info)},
    {"ts_box_type", FPTR(ts_box_type)},
    {"ts_enum_create", FPTR(ts_enum_create)},
    {"ts_enum_add_member", FPTR(ts_enum_add_member)},
    {"ts_enum_freeze", FPTR(ts_enum_freeze)},

#ifdef LAMBDA_RUBY
    // Ruby runtime functions
    {"rb_to_int", FPTR(rb_to_int)},
    {"rb_to_float", FPTR(rb_to_float)},
    {"rb_to_str", FPTR(rb_to_str)},
    {"rb_to_bool", FPTR(rb_to_bool)},
    {"rb_to_s", FPTR(rb_to_s)},
    {"rb_to_i", FPTR(rb_to_i)},
    {"rb_to_f", FPTR(rb_to_f)},
    {"rb_is_truthy", FPTR(rb_is_truthy)},
    {"rb_add", FPTR(rb_add)},
    {"rb_subtract", FPTR(rb_subtract)},
    {"rb_multiply", FPTR(rb_multiply)},
    {"rb_divide", FPTR(rb_divide)},
    {"rb_modulo", FPTR(rb_modulo)},
    {"rb_power", FPTR(rb_power)},
    {"rb_negate", FPTR(rb_negate)},
    {"rb_positive", FPTR(rb_positive)},
    {"rb_bit_not", FPTR(rb_bit_not)},
    {"rb_bit_and", FPTR(rb_bit_and)},
    {"rb_bit_or", FPTR(rb_bit_or)},
    {"rb_bit_xor", FPTR(rb_bit_xor)},
    {"rb_lshift", FPTR(rb_lshift)},
    {"rb_rshift", FPTR(rb_rshift)},
    {"rb_eq", FPTR(rb_eq)},
    {"rb_ne", FPTR(rb_ne)},
    {"rb_lt", FPTR(rb_lt)},
    {"rb_le", FPTR(rb_le)},
    {"rb_gt", FPTR(rb_gt)},
    {"rb_ge", FPTR(rb_ge)},
    {"rb_cmp", FPTR(rb_cmp)},
    {"rb_case_eq", FPTR(rb_case_eq)},
    {"rb_getattr", FPTR(rb_getattr)},
    {"rb_setattr", FPTR(rb_setattr)},
    {"rb_new_object", FPTR(rb_new_object)},
    {"rb_array_new", FPTR(rb_array_new)},
    {"rb_array_push", FPTR(rb_array_push)},
    {"rb_array_get", FPTR(rb_array_get)},
    {"rb_array_set", FPTR(rb_array_set)},
    {"rb_array_length", FPTR(rb_array_length)},
    {"rb_array_pop", FPTR(rb_array_pop)},
    {"rb_hash_new", FPTR(rb_hash_new)},
    {"rb_hash_get", FPTR(rb_hash_get)},
    {"rb_hash_set", FPTR(rb_hash_set)},
    {"rb_range_new", FPTR(rb_range_new)},
    {"rb_subscript_get", FPTR(rb_subscript_get)},
    {"rb_subscript_set", FPTR(rb_subscript_set)},
    {"rb_string_concat", FPTR(rb_string_concat)},
    {"rb_string_repeat", FPTR(rb_string_repeat)},
    {"rb_get_iterator", FPTR(rb_get_iterator)},
    {"rb_iterator_next", FPTR(rb_iterator_next)},
    {"rb_is_stop_iteration", FPTR(rb_is_stop_iteration)},
    {"rb_set_module_var", FPTR(rb_set_module_var)},
    {"rb_get_module_var", FPTR(rb_get_module_var)},
    {"rb_reset_module_vars", FPTR(rb_reset_module_vars)},
    {"rb_puts_one", FPTR(rb_puts_one)},
    {"rb_print_one", FPTR(rb_print_one)},
    {"rb_p_one", FPTR(rb_p_one)},
    {"rb_builtin_len", FPTR(rb_builtin_len)},
    {"rb_builtin_type", FPTR(rb_builtin_type)},
    {"rb_builtin_rand", FPTR(rb_builtin_rand)},
    {"rb_builtin_require_relative", FPTR(rb_builtin_require_relative)},
    // Phase 2: Class system
    {"rb_class_create", FPTR(rb_class_create)},
    {"rb_class_add_method", FPTR(rb_class_add_method)},
    {"rb_class_new_instance", FPTR(rb_class_new_instance)},
    {"rb_is_class", FPTR(rb_is_class)},
    {"rb_is_instance", FPTR(rb_is_instance)},
    {"rb_get_class", FPTR(rb_get_class)},
    {"rb_instance_getattr", FPTR(rb_instance_getattr)},
    {"rb_instance_setattr", FPTR(rb_instance_setattr)},
    {"rb_method_lookup", FPTR(rb_method_lookup)},
    {"rb_super_lookup", FPTR(rb_super_lookup)},
    {"rb_attr_reader", FPTR(rb_attr_reader)},
    {"rb_attr_writer", FPTR(rb_attr_writer)},
    {"rb_attr_accessor", FPTR(rb_attr_accessor)},
    // Phase 2: Block / Proc / Lambda
    {"rb_block_call", FPTR(rb_block_call)},
    {"rb_block_call_0", FPTR(rb_block_call_0)},
    {"rb_block_call_1", FPTR(rb_block_call_1)},
    {"rb_block_call_2", FPTR(rb_block_call_2)},
    // Phase 2: Iterator methods
    {"rb_array_each", FPTR(rb_array_each)},
    {"rb_array_map", FPTR(rb_array_map)},
    {"rb_array_select", FPTR(rb_array_select)},
    {"rb_array_reject", FPTR(rb_array_reject)},
    {"rb_array_reduce", FPTR(rb_array_reduce)},
    {"rb_array_each_with_index", FPTR(rb_array_each_with_index)},
    {"rb_array_any", FPTR(rb_array_any)},
    {"rb_array_all", FPTR(rb_array_all)},
    {"rb_array_find", FPTR(rb_array_find)},
    {"rb_int_times", FPTR(rb_int_times)},
    {"rb_int_upto", FPTR(rb_int_upto)},
    {"rb_int_downto", FPTR(rb_int_downto)},
    {"rb_array_flat_map", FPTR(rb_array_flat_map)},
    {"rb_array_each_with_object", FPTR(rb_array_each_with_object)},
    {"rb_array_sort_by", FPTR(rb_array_sort_by)},
    {"rb_array_min_by", FPTR(rb_array_min_by)},
    {"rb_array_max_by", FPTR(rb_array_max_by)},
    {"rb_array_reduce_no_init", FPTR(rb_array_reduce_no_init)},
    {"rb_hash_each", FPTR(rb_hash_each)},
    {"rb_hash_map", FPTR(rb_hash_map)},
    {"rb_hash_select", FPTR(rb_hash_select)},
    // Phase 3: Built-in method dispatchers
    {"rb_string_method", FPTR(rb_string_method)},
    {"rb_array_method", FPTR(rb_array_method)},
    {"rb_hash_method", FPTR(rb_hash_method)},
    {"rb_int_method", FPTR(rb_int_method)},
    {"rb_float_method", FPTR(rb_float_method)},
    // Phase 4: Exception handling
    {"rb_raise", FPTR(rb_raise)},
    {"rb_check_exception", FPTR(rb_check_exception)},
    {"rb_clear_exception", FPTR(rb_clear_exception)},
    {"rb_new_exception", FPTR(rb_new_exception)},
    {"rb_exception_get_type", FPTR(rb_exception_get_type)},
    {"rb_exception_get_message", FPTR(rb_exception_get_message)},
    // Phase 4: Dynamic dispatch / introspection
    {"rb_respond_to", FPTR(rb_respond_to)},
    {"rb_send", FPTR(rb_send)},
    // defined? keyword
    {"rb_defined", FPTR(rb_defined)},
    // File I/O
    {"rb_file_read", FPTR(rb_file_read)},
    {"rb_file_write", FPTR(rb_file_write)},
    {"rb_file_exist", FPTR(rb_file_exist)},
    // Regex
    {"rb_regex_new", FPTR(rb_regex_new)},
    {"rb_regex_match", FPTR(rb_regex_match)},
    {"rb_regex_test", FPTR(rb_regex_test)},
    {"rb_regex_scan", FPTR(rb_regex_scan)},
    {"rb_regex_gsub", FPTR(rb_regex_gsub)},
    {"rb_regex_sub", FPTR(rb_regex_sub)},
    {"rb_is_regex", FPTR(rb_is_regex)},
    {"rb_module_include", FPTR(rb_module_include)},
    // method_missing
    {"rb_call_method_missing", FPTR(rb_call_method_missing)},
    // Struct
    {"rb_struct_new", FPTR(rb_struct_new)},
    {"rb_struct_init", FPTR(rb_struct_init)},
    {"rb_is_struct", FPTR(rb_is_struct)},
    {"rb_struct_members", FPTR(rb_struct_members)},
#endif // LAMBDA_RUBY
};

const int jit_runtime_import_count = sizeof(jit_runtime_imports) / sizeof(jit_runtime_imports[0]);

#else
// LAMBDA_STATIC build (dylib): provide empty stubs
JitImport jit_runtime_imports[] = {{NULL, NULL}};
const int jit_runtime_import_count = 0;
#endif // !LAMBDA_STATIC
