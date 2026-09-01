#pragma once

// js_runtime_internal.hpp - shared declarations for the split JS runtime.

#include "js_runtime.h"
#include "../dom/dom.h"
#include "../dom/dom_events.h"
#include "../dom/dom_cssom.h"
#include "js_typed_array.h"
#include "js_event_loop.h"
#include "js_error_codes.h"
#include "js_property_attrs.h"
#include "js_props.h"
#include "js_class.h"
#include "js_coerce.h"
#include "js_runtime_state.hpp"
#include "js_function.hpp"
#include "js_builtin_catalog.hpp"
#include "../lambda-data.hpp"
#include "../core/name_pool.hpp"
#include "../core/lambda-decimal.hpp"
#include "../runtime/transpiler.hpp"
#include "../runtime/module_registry.h"
#include "../core/lambda_typed.hpp"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include "../../lib/str.h"
#include "../../lib/utf.h"
#include "../../lib/windows_compat.h"
#include <cstring>
#include <cmath>
#include "../../lib/mem.h"
#include <cstdio>
#include <cstdlib>
#include <uv.h>
#include <cctype>
#include <string>
#include <unordered_map>
#include <map>
#include <re2/re2.h>
#include <utf8proc.h>
#ifndef _WIN32
#include <execinfo.h>
#endif



#ifndef JS_RUNTIME_INTERNAL_HPP_DECLS
#define JS_RUNTIME_INTERNAL_HPP_DECLS

char* js_skip_ecma_whitespace(char* start, char* end);

extern "C" Item js_get_generator_shared_proto(bool is_async);
extern "C" JsFunction* js_alloc_gc_function_object(void);
void js_function_finalize_capabilities(JsFunction* fn);
// The generic dispatcher in its call-entry form; the classifier's fallback
// stamps this when no narrower finalized protocol covers the function.
Item js_call_entry_generic(Item fn_item, Item this_val, Item* args, int argc,
        uint64_t* result_home, bool args_prerooted);
Item js_call_entry_bound(Item fn_item, Item this_val, Item* args, int argc,
        uint64_t* result_home, bool args_prerooted);
Item js_construct_entry_ordinary(Item fn_item, Item* args, int argc,
        Item new_target, uint64_t* result_home, bool args_prerooted);
Item js_construct_entry_native(Item fn_item, Item* args, int argc,
        Item new_target, uint64_t* result_home, bool args_prerooted);
Item js_construct_entry_bound(Item fn_item, Item* args, int argc,
        Item new_target, uint64_t* result_home, bool args_prerooted);
Item js_native_construct_via_call_body(Item callee, Item* args, int argc,
    Item new_target, uint64_t* result_home);
Item js_typed_array_base_call_body(Item callee, Item this_value,
    Item* args, int argc, uint64_t* result_home);
Item js_typed_array_base_construct_body(Item callee, Item* args, int argc,
    Item new_target, uint64_t* result_home);
bool js_property_ops_has_property(Item object, Item key, TypeId type,
                                Item* out_result);
bool js_property_ops_delete_property(Item object, Item key, Item* out_result);
bool js_property_ops_own_property_names(Item object, Item* out_result);
bool js_property_ops_own_property_descriptor(Item object, Item name,
    String* name_str, TypeId type, Item* out_result);
bool js_ta_define_own_numeric_index(Item object, Item key, Item desc,
    bool* out_handled, Item* out_error);
// Pick the thinnest entry whose protocol still covers this callee's shape.
// Only the classifier above may call this.

// v22 / P8 + Js58.2: Maximum index/capacity gap considered for dense array
// expansion before forcing sparse companion-map storage. Js58.2 restores the
// ES-scale cap and relies on density conversion in js_runtime.cpp to keep
// low-density writes such as `arr[999999] = ...` sparse instead of
// dense-filling almost one million holes.
#define SPARSE_GAP_MAX 1000000

// Forward declarations for Unicode normalization (implemented in utf_string.cpp)
extern "C" char* normalize_utf8proc_nfc(const char* str, int len, int* out_len);
extern "C" char* normalize_utf8proc_nfd(const char* str, int len, int* out_len);
extern "C" char* normalize_utf8proc_nfkc(const char* str, int len, int* out_len);
extern "C" char* normalize_utf8proc_nfkd(const char* str, int len, int* out_len);

extern TypeMap EmptyMap;

extern "C" bool js_func_is_builtin_ctor(Item fn);
extern "C" bool js_function_has_own_prototype(Item fn);
extern "C" Item js_elements_get_custom_proto(Item arr);
extern "C" void js_child_process_reset();
extern "C" void js_fs_reset();
extern "C" void js_util_reset();


Item _map_read_field(ShapeEntry* field, void* map_data);
Item _map_get(TypeMap* map_type, void* map_data, const char *key, bool *is_found);

Map* js_resolve_object_prototype();
Item js_map_shape_lookup(Map* m, const char* key_str, int key_len, bool* out_found = nullptr);
// Own-property lookup that folds the "was it present?" flag into the result.
// js_map_own_or yields `fallback` for an absent property; js_map_own_flag
// coerces to boolean, yielding `dflt` when absent.
static inline Item js_map_own_or(Map* m, const char* key_str, int key_len, Item fallback) {
    bool found = false;
    Item value = js_map_shape_lookup(m, key_str, key_len, &found);
    return found ? value : fallback;
}
static inline bool js_map_own_flag(Map* m, const char* key_str, int key_len, bool dflt) {
    bool found = false;
    Item value = js_map_shape_lookup(m, key_str, key_len, &found);
    return found ? it2b(js_to_boolean(value)) : dflt;
}
Item js_check_array_sym_iterator();
extern "C" void js_intrinsic_note_property_mutation(Item object, Item key);
void js_regex_cache_reset();
void js_module_cache_reset();
void js_reset_transient_call_state();
void js_reset_heap_bound_runtime_state(bool full_reset);
void js_decimal_number_egress_warning_reset();
void js_assert_batch_runtime_state_clear(const char* reset_name, bool include_heap_bound);
void js_reset_math_object();
void js_reset_json_object();
extern "C" void js_reset_intl_object();
void js_reset_console_object();
void js_reset_reflect_object();
void js_reset_atomics_object();
void js_reset_262_object();
void js_reset_proto_key();
void js_func_cache_reset();
extern "C" void js_function_set_prototype(Item fn_item, Item proto);
void js_builtin_cache_reset();
void js_deep_batch_reset();

double js_get_number(Item value);
Item js_make_number(double d);
int32_t js_to_int32(double d);

extern "C" Item js_number_function(Item value);
enum JsNumericPrototypeOp {
    JS_NUMERIC_TO_STRING,
    JS_NUMERIC_VALUE_OF,
    JS_NUMERIC_TO_FIXED,
    JS_NUMERIC_TO_PRECISION,
    JS_NUMERIC_TO_EXPONENTIAL,
    JS_NUMERIC_TO_LOCALE_STRING,
};
Item js_numeric_prototype_algorithm(Item number, JsNumericPrototypeOp operation,
    Item* args, int arg_count);
Item js_iterator_prototype_for_object(Item object);
extern "C" bool js_typed_array_is_out_of_bounds_item(Item ta_item);
extern "C" Item js_object_define_property(Item obj, Item name, Item descriptor);
extern "C" Item js_has_own_property(Item obj, Item key);
extern "C" Item js_object_has_own(Item obj, Item key);
extern "C" Item js_object_prototype_has_own_property(Item this_val, Item key);

void js_double_to_string(double d, char* out, int out_size);
bool js_ta_key_canonical_numeric(Item key, double* numeric_index, bool* is_negative_zero);
bool js_ta_numeric_index_valid(Item object, double numeric_index, bool is_negative_zero, int* out_index);
bool js_ta_proto_chain_set(Item object, Item key, Item value, Item receiver,
                           bool bypass_accessor_dispatch, Item* out_result);
bool js_array_ta_proto_numeric_set(Item array, Item key, bool* no_op);

static inline bool js_is_symbol(Item v) {
    if (get_type_id(v) == LMD_TYPE_SYMBOL) return true;
    if (get_type_id(v) != LMD_TYPE_INT) return false;
    return it2i(v) <= -(int64_t)JS_SYMBOL_BASE;
}

static inline bool js_is_bigint(Item v) {
    if (get_type_id(v) != LMD_TYPE_DECIMAL) return false;
    Decimal* dec = (Decimal*)(v.item & 0x00FFFFFFFFFFFFFFULL);
    return dec && dec->unlimited == DECIMAL_BIGINT;
}

static inline bool js_is_native_bigint_egress(Item v) {
    TypeId type = get_type_id(v);
    return type == LMD_TYPE_INT64 || type == LMD_TYPE_UINT64;
}

static inline bool js_is_bigint_egress(Item v) {
    return js_is_bigint(v) || js_is_native_bigint_egress(v);
}

static inline Item js_native_bigint_to_bigint(Item v) {
    TypeId type = get_type_id(v);
    // Lambda int64/uint64 have a JS BigInt FFI face; canonicalize before using
    // BigInt runtime helpers, which operate on the mpdec-unlimited backing.
    if (type == LMD_TYPE_INT64) return bigint_from_int64(it2l(v));
    if (type == LMD_TYPE_UINT64) {
        uint64_t* ptr = (uint64_t*)(uintptr_t)(v.item & 0x00FFFFFFFFFFFFFFULL);
        if (!ptr) return ItemError;
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)*ptr);
        return bigint_from_string(buf, len);
    }
    return v;
}

static inline Item js_check_bigint_arithmetic(Item left, Item right) {
    bool lbig = js_is_bigint(left);
    bool rbig = js_is_bigint(right);
    if (lbig != rbig) {
        return js_throw_type_error("Cannot mix BigInt and other types, use explicit conversions");
    }
    return ItemNull;
}

static inline bool js_is_deleted_sentinel(Item val) {
    return lam::is_hole_sentinel(val);
}

static inline bool js_key_is_symbol(Item key) {
    if (get_type_id(key) != LMD_TYPE_INT) return false;
    return it2i(key) <= -(int64_t)JS_SYMBOL_BASE;
}

extern "C" NameId js_symbol_name_id(Item sym);

static inline Item js_symbol_to_key(Item sym) {
    NameId semantic_id = js_symbol_name_id(sym);
    if (semantic_id != NAME_ID_NONE) {
        // Every Symbol has a registered semantic NameRecord.  Property
        // identity may never depend on the historical diagnostic encoding.
        NameRef semantic_key = name_pool_resolve_id(
            context ? context->name_pool : NULL, semantic_id);
        if (semantic_key) return (Item){.item = s2it(semantic_key)};
    }
    // An encoded Symbol without a registered NameRecord is invalid runtime
    // state; manufacturing a printable string here would alias a user key.
    return ItemNull;
}

#endif
