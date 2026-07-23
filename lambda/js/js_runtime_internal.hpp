#pragma once

// js_runtime_internal.hpp - shared declarations for the split JS runtime.

#include "js_runtime.h"
#include "js_dom.h"
#include "js_dom_events.h"
#include "js_cssom.h"
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
#include "../core/lambda-decimal.hpp"
#include "../runtime/transpiler.hpp"
#include "../runtime/module_registry.h"
#include "../core/lambda_typed.hpp"
#include "../../lib/log.h"
#include "../../lib/hashmap.h"
#include "../../lib/str.h"
#include "../../lib/utf.h"
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
#else
// Windows stubs for POSIX functions
#include <direct.h>
#include <io.h>
#include <stdlib.h>
static inline int setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name, value) == 0 ? 0 : -1;
}
static inline int unsetenv(const char* name) {
    return _putenv_s(name, "") == 0 ? 0 : -1;
}
static inline void* memmem(const void* haystack, size_t hlen, const void* needle, size_t nlen) {
    if (nlen == 0) return (void*)haystack;
    if (nlen > hlen) return NULL;
    const char* h = (const char*)haystack;
    const char* n = (const char*)needle;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        if (memcmp(h + i, n, nlen) == 0) return (void*)(h + i);
    }
    return NULL;
}
#endif



#ifndef JS_RUNTIME_INTERNAL_HPP_DECLS
#define JS_RUNTIME_INTERNAL_HPP_DECLS

extern "C" Item js_get_generator_shared_proto(bool is_async);
extern "C" JsFunction* js_alloc_gc_function_object(void);

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
extern "C" Item js_lookup_builtin_method(TypeId type, const char* name, int len);
extern "C" Item js_array_get_custom_proto(Item arr);
extern "C" void js_child_process_reset();
extern "C" void js_fs_reset();
extern "C" void js_path_reset();
extern "C" void js_os_reset();
extern "C" void js_url_module_reset();
extern "C" void js_util_reset();


Item _map_read_field(ShapeEntry* field, void* map_data);
Item _map_get(TypeMap* map_type, void* map_data, const char *key, bool *is_found);

bool js_runtime_trace_enabled();
void js_strict_throw_property_error(const char* reason, const char* prop_name, int prop_len);
Map* js_resolve_object_prototype();
Item js_map_get_fast(Map* m, const char* key_str, int key_len, bool* out_found = nullptr);
Item js_check_array_sym_iterator();
extern "C" void js_intrinsic_note_property_mutation(Item object, Item key);
void js_regex_cache_reset();
void js_module_cache_reset();
void js_reset_transient_call_state();
void js_reset_heap_bound_runtime_state();
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

extern "C" Item js_property_get_str(Item object, const char* key, int key_len);
extern "C" Item js_number_function(Item value);
extern "C" bool js_typed_array_is_out_of_bounds_item(Item ta_item);
extern "C" Item js_object_define_property(Item obj, Item name, Item descriptor);
extern "C" Item js_has_own_property(Item obj, Item key);
extern "C" Item js_object_has_own(Item obj, Item key);
extern "C" Item js_object_prototype_has_own_property(Item this_val, Item key);

void js_double_to_string(double d, char* out, int out_size);
bool js_ta_key_canonical_numeric(Item key, double* numeric_index, bool* is_negative_zero);
bool js_ta_numeric_index_valid(Item object, double numeric_index, bool is_negative_zero, int* out_index);
bool js_ta_proto_chain_set(Item object, Item key, Item value);
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

static inline bool js_check_bigint_arithmetic(Item left, Item right) {
    bool lbig = js_is_bigint(left);
    bool rbig = js_is_bigint(right);
    if (lbig != rbig) {
        js_throw_type_error("Cannot mix BigInt and other types, use explicit conversions");
        return true;
    }
    return false;
}

static inline bool js_is_deleted_sentinel(Item val) {
    return lam::is_hole_sentinel(val);
}

static inline bool js_key_is_symbol(Item key) {
    if (get_type_id(key) != LMD_TYPE_INT) return false;
    return it2i(key) <= -(int64_t)JS_SYMBOL_BASE;
}

static inline Item js_symbol_to_key(Item sym) {
    int64_t id = -(it2i(sym) + (int64_t)JS_SYMBOL_BASE);
    char buf[32];
    snprintf(buf, sizeof(buf), "__sym_%lld", (long long)id);
    return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
}

namespace {
class ScopedProxyReceiver {
public:
    explicit ScopedProxyReceiver(Item recv) : prev_(js_proxy_receiver) {
        if (!js_proxy_receiver.item) js_proxy_receiver = recv;
    }
    ~ScopedProxyReceiver() { js_proxy_receiver = prev_; }
    ScopedProxyReceiver(const ScopedProxyReceiver&) = delete;
    ScopedProxyReceiver& operator=(const ScopedProxyReceiver&) = delete;
private:
    Item prev_;
};
} // namespace

#endif
