/**
 * JavaScript Process I/O and Global Functions for Lambda v5
 *
 * Implements:
 * - process.stdout.write(str)
 * - process.hrtime.bigint() (as float64 nanoseconds)
 * - process.argv
 * - parseInt, parseFloat, isNaN, isFinite
 * - console.log with multiple arguments
 */
#include "js_runtime.h"
#include "js_typed_array.h"
#include "js_dom_events.h"
#include "js_property_attrs.h"
#include "js_props.h"
#include "js_class.h"
#include "js_coerce.h"
#include "../lambda-data.hpp"
#include "../lambda-decimal.hpp"
#include "../lambda.hpp"
#include "../transpiler.hpp"

extern "C" Item js_to_property_key(Item key);

#include "../format/format.h"
#include "../../lib/log.h"
#include "../../lib/url.h"
#include "../../lib/file.h"
#include "../../lib/mem.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <time.h>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#include <psapi.h>
#define getpid _getpid
#else
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#endif

// forward declaration for JSON parser
extern Item parse_json_to_item(Input* input, const char* json_string);
extern Item parse_json_to_item_strict(Input* input, const char* json_string, bool* ok);

// forward declarations for functions used by ES2020 descriptor helpers below
static inline Item make_js_undefined();
extern "C" void js_defprop_set_marker(Item obj, Item key, Item value);
extern "C" bool js_defprop_has_marker(Item obj, Item key);
static bool js_require_object_type(Item arg, const char* method_name);
static Item js_defprop_get_marker(Item obj, const char* key, int keylen, bool* found);
extern "C" Item js_strict_equal(Item left, Item right);
extern "C" Item js_object_is(Item left, Item right);

// ES2020: Descriptor type helpers
static bool is_data_descriptor(Item desc) {
    if (get_type_id(desc) != LMD_TYPE_MAP) return false;
    Item value_k = (Item){.item = s2it(heap_create_name("value", 5))};
    Item writable_k = (Item){.item = s2it(heap_create_name("writable", 8))};
    return it2b(js_in(value_k, desc)) || it2b(js_in(writable_k, desc));
}

static bool is_accessor_descriptor(Item desc) {
    if (get_type_id(desc) != LMD_TYPE_MAP) return false;
    Item get_k = (Item){.item = s2it(heap_create_name("get", 3))};
    Item set_k = (Item){.item = s2it(heap_create_name("set", 3))};
    return it2b(js_in(get_k, desc)) || it2b(js_in(set_k, desc));
}

static bool is_generic_descriptor(Item desc) {
    return get_type_id(desc) == LMD_TYPE_MAP && !is_data_descriptor(desc) && !is_accessor_descriptor(desc);
}
// ES2020 §9.1.6.3 ValidateAndApplyPropertyDescriptor
static Item ValidateAndApplyPropertyDescriptor(Item obj, Item name, Item descriptor) {
    if (!js_require_object_type(obj, "defineProperty")) return ItemNull;
    if (obj.item == 0) return obj;
    // Object.defineProperty performs [[DefineOwnProperty]], not [[Set]]. Any internal
    // js_property_set calls in this function must bypass accessor dispatch (which
    // would otherwise trigger inherited-setter logic and reject writes for objects
    // whose prototype chain has an accessor with no setter).
    extern bool js_skip_accessor_dispatch;
    bool _prev_skip_accessor_dispatch = js_skip_accessor_dispatch;
    js_skip_accessor_dispatch = true;
    struct _RestoreSkip { bool* p; bool v; ~_RestoreSkip() { *p = v; } }
        _restore_skip{&js_skip_accessor_dispatch, _prev_skip_accessor_dispatch};
    // v18m: coerce property name to property key (ES2020 §7.1.14 ToPropertyKey)
    // Symbols stay as internal __sym_N keys; others coerced to string.
    TypeId name_type = get_type_id(name);
    if (name_type != LMD_TYPE_STRING) {
        name = js_to_property_key(name);
    }

    // ES §9.4.2.1: Array exotic objects — special [[DefineOwnProperty]] for "length"
    // If obj is an array and name is "length", validate the value as a valid array length.
    if (get_type_id(obj) == LMD_TYPE_ARRAY && get_type_id(name) == LMD_TYPE_STRING) {
        String* ns = it2s(name);
        // J39-7: ES §9.4.2.1 step 3.f.iii — for an array index P >= length,
        // if length is non-writable (__nw_length marker on companion map),
        // throw TypeError. Index names are decimal digit strings.
        if (ns && ns->len > 0 && ns->len <= 10 &&
            (ns->len == 1 || ns->chars[0] != '0')) {
            bool _is_idx = true;
            uint64_t _idx = 0;
            for (uint32_t _i = 0; _i < ns->len; _i++) {
                char _c = ns->chars[_i];
                if (_c < '0' || _c > '9') { _is_idx = false; break; }
                _idx = _idx * 10 + (uint64_t)(_c - '0');
            }
            if (_is_idx && _idx <= 0xFFFFFFFEULL) {
                bool _nw_len = false;
                if (obj.array && obj.array->extra != 0) {
                    Map* _pm = (Map*)(uintptr_t)obj.array->extra;
                    bool _f = false;
                    Item _v = js_map_get_fast_ext(_pm, "__nw_length", 11, &_f);
                    if (_f && _v.item != JS_DELETED_SENTINEL_VAL && js_is_truthy(_v)) _nw_len = true;
                }
                if (_nw_len && obj.array && (uint64_t)obj.array->length <= _idx) {
                    Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                    Item msg = (Item){.item = s2it(heap_create_name("Cannot add property: array length is non-writable"))};
                    js_throw_value(js_new_error_with_name(tn, msg));
                    return obj;
                }
            }
        }
        if (ns && ns->len == 6 && strncmp(ns->chars, "length", 6) == 0) {
            Item val_k = (Item){.item = s2it(heap_create_name("value", 5))};
            if (it2b(js_in(val_k, descriptor))) {
                Item len_val = js_property_get(descriptor, val_k);
                // ToUint32(Desc.[[Value]]) must equal ToNumber(Desc.[[Value]]) per ES §15.4.5.1
                Item num_item = js_to_number(len_val);
                // If js_to_number threw (e.g. ToPrimitive fails) let that exception propagate
                extern int js_check_exception(void);
                if (js_check_exception()) return obj;
                TypeId nt = get_type_id(num_item);
                double d = (nt == LMD_TYPE_FLOAT) ? it2d(num_item) :
                           (nt == LMD_TYPE_INT) ? (double)it2i(num_item) : NAN;
                // NaN means ToPrimitive returned a non-numeric non-convertible value → TypeError
                if (isnan(d)) {
                    Item tn = (Item){.item = s2it(heap_create_name("RangeError"))};
                    Item msg = (Item){.item = s2it(heap_create_name("Invalid array length"))};
                    js_throw_value(js_new_error_with_name(tn, msg));
                    return obj;
                }
                uint32_t u32 = (uint32_t)d;
                if ((double)u32 != d) {
                    Item tn = (Item){.item = s2it(heap_create_name("RangeError"))};
                    Item msg = (Item){.item = s2it(heap_create_name("Invalid array length"))};
                    js_throw_value(js_new_error_with_name(tn, msg));
                    return obj;
                }
                // J39-7: ES §9.4.2.4 ArraySetLength step 16/17 — if existing
                // length is non-writable (__nw_length marker on companion map),
                // reject any value change. Writable→non-writable is allowed
                // (handled by step-7d below for the writable attribute).
                bool _nw_len = false;
                if (obj.array && obj.array->extra != 0) {
                    Map* _pm = (Map*)(uintptr_t)obj.array->extra;
                    bool _nw_found = false;
                    Item _nwv = js_map_get_fast_ext(_pm, "__nw_length", 11, &_nw_found);
                    if (_nw_found && _nwv.item != JS_DELETED_SENTINEL_VAL && js_is_truthy(_nwv)) _nw_len = true;
                }
                if (_nw_len && (uint32_t)obj.array->length != u32) {
                    Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                    Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property: length"))};
                    js_throw_value(js_new_error_with_name(tn, msg));
                    return obj;
                }
            }
        }
    }

    // v18l: TypeError if descriptor is not an object (ES5 8.10.5 ToPropertyDescriptor step 1)
    // Any object type is valid (Map, Function, Array, Element, etc.); reject primitives.
    TypeId desc_type = get_type_id(descriptor);
    if (desc_type != LMD_TYPE_MAP && desc_type != LMD_TYPE_FUNC &&
        desc_type != LMD_TYPE_ARRAY && desc_type != LMD_TYPE_ELEMENT) {
        Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item msg = (Item){.item = s2it(heap_create_name("Property description must be an object"))};
        js_throw_value(js_new_error_with_name(tn, msg));
        return obj;
    }
    // v18l: Validate descriptor — mixed accessor+data is TypeError (ES5 8.10.5 step 9)
    {
        Item get_k = (Item){.item = s2it(heap_create_name("get", 3))};
        Item set_k = (Item){.item = s2it(heap_create_name("set", 3))};
        Item val_k = (Item){.item = s2it(heap_create_name("value", 5))};
        Item wri_k = (Item){.item = s2it(heap_create_name("writable", 8))};
        bool has_get = it2b(js_in(get_k, descriptor));
        bool has_set = it2b(js_in(set_k, descriptor));
        bool has_val = it2b(js_in(val_k, descriptor));
        bool has_wri = it2b(js_in(wri_k, descriptor));
        if ((has_get || has_set) && (has_val || has_wri)) {
            Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
            Item msg = (Item){.item = s2it(heap_create_name("Invalid property descriptor. Cannot both specify accessors and a value or writable attribute"))};
            js_throw_value(js_new_error_with_name(tn, msg));
            return obj;
        }
        // v18l: Non-callable getter/setter is TypeError (ES5 8.10.5 step 7.b / 8.b)
        if (has_get) {
            Item getter = js_property_get(descriptor, get_k);
            if (get_type_id(getter) != LMD_TYPE_FUNC && get_type_id(getter) != LMD_TYPE_UNDEFINED) {
                Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                Item msg = (Item){.item = s2it(heap_create_name("Getter must be a function"))};
                js_throw_value(js_new_error_with_name(tn, msg));
                return obj;
            }
        }
        if (has_set) {
            Item setter = js_property_get(descriptor, set_k);
            if (get_type_id(setter) != LMD_TYPE_FUNC && get_type_id(setter) != LMD_TYPE_UNDEFINED) {
                Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                Item msg = (Item){.item = s2it(heap_create_name("Setter must be a function"))};
                js_throw_value(js_new_error_with_name(tn, msg));
                return obj;
            }
        }
    }
    // v18n: check is_new_property BEFORE setting any value (since js_property_set creates the property)
    bool is_new_property = !it2b(js_has_own_property(obj, name));
    // also check accessor markers: accessor-only properties on arrays may not be detected by js_has_own_property
    if (is_new_property && get_type_id(obj) == LMD_TYPE_ARRAY) {
        Item nsc = js_to_string(name);
        if (get_type_id(nsc) == LMD_TYPE_STRING) {
            String* ns = it2s(nsc);
            if (ns && ns->len > 0 && ns->len < 200) {
                // AT-3: accessors on arrays are stored on the companion map under
                // the digit-string name with the IS_ACCESSOR shape flag (post-AT-1).
                Map* pm = obj.array && obj.array->extra ? (Map*)(uintptr_t)obj.array->extra : nullptr;
                if (pm) {
                    Item pm_item = (Item){.map = pm};
                    ShapeEntry* _se = js_find_shape_entry(pm_item, ns->chars, (int)ns->len);
                    if (_se && jspd_is_accessor(_se)) is_new_property = false;
                }
            }
        }
    }

    // ES2020 §9.1.6.3 step 7: If existing property is non-configurable, reject certain changes
    if (!is_new_property) {
        Item name_str_check = js_to_string(name);
        if (get_type_id(name_str_check) == LMD_TYPE_STRING) {
            String* ns_check = it2s(name_str_check);
            if (ns_check && ns_check->len > 0 && ns_check->len < 200) {
                // Stage A3.3: shape-flag-first attribute query (falls back to
                // legacy __nc_/__ne_/__nw_ marker probe).
                bool is_non_configurable = !js_props_obj_query_configurable(
                    obj, ns_check->chars, (int)ns_check->len);

                if (is_non_configurable) {
                    // 7a: reject if desc.[[Configurable]] is true
                    Item cfg_key = (Item){.item = s2it(heap_create_name("configurable", 12))};
                    if (it2b(js_in(cfg_key, descriptor))) {
                        Item cfg_val = js_property_get(descriptor, cfg_key);
                        if (js_is_truthy(cfg_val)) {
                            Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                            Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property: configurable"))};
                            js_throw_value(js_new_error_with_name(tn, msg));
                            return obj;
                        }
                    }
                    // 7b: reject if desc.[[Enumerable]] differs from current
                    Item enum_key = (Item){.item = s2it(heap_create_name("enumerable", 10))};
                    if (it2b(js_in(enum_key, descriptor))) {
                        Item enum_val = js_property_get(descriptor, enum_key);
                        bool desc_enum = js_is_truthy(enum_val);
                        // Stage A3.3: shape-flag-first attribute query.
                        bool cur_enum = js_props_obj_query_enumerable(
                            obj, ns_check->chars, (int)ns_check->len);
                        if (desc_enum != cur_enum) {
                            Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                            Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property: enumerable"))};
                            js_throw_value(js_new_error_with_name(tn, msg));
                            return obj;
                        }
                    }
                    // Check if current is accessor or data property
                    // AT-3: accessors are stored as JsAccessorPair with IS_ACCESSOR
                    // shape flag (post-AT-1). Probe shape entry only.
                    ShapeEntry* _se_acc_chk = js_find_shape_entry(obj, ns_check->chars, (int)ns_check->len);
                    bool cur_is_accessor = (_se_acc_chk && jspd_is_accessor(_se_acc_chk));

                    Item val_key_check = (Item){.item = s2it(heap_create_name("value", 5))};
                    Item wri_key_check = (Item){.item = s2it(heap_create_name("writable", 8))};
                    Item get_key_check = (Item){.item = s2it(heap_create_name("get", 3))};
                    Item set_key_check = (Item){.item = s2it(heap_create_name("set", 3))};
                    bool desc_is_data = it2b(js_in(val_key_check, descriptor)) || it2b(js_in(wri_key_check, descriptor));
                    bool desc_is_accessor = it2b(js_in(get_key_check, descriptor)) || it2b(js_in(set_key_check, descriptor));

                    // 7c: reject if converting between accessor and data
                    if (cur_is_accessor && desc_is_data) {
                        Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                        Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property: accessor to data"))};
                        js_throw_value(js_new_error_with_name(tn, msg));
                        return obj;
                    }
                    if (!cur_is_accessor && desc_is_accessor) {
                        Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                        Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property: data to accessor"))};
                        js_throw_value(js_new_error_with_name(tn, msg));
                        return obj;
                    }

                    if (!cur_is_accessor) {
                        // 7d: data property — check writable constraints.
                        // Stage A3.3: shape-flag-first attribute query.
                        bool is_non_writable = !js_props_obj_query_writable(
                            obj, ns_check->chars, (int)ns_check->len);

                        if (is_non_writable) {
                            // reject if trying to make writable
                            if (it2b(js_in(wri_key_check, descriptor))) {
                                Item wri_val = js_property_get(descriptor, wri_key_check);
                                if (js_is_truthy(wri_val)) {
                                    Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                                    Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property: writable"))};
                                    js_throw_value(js_new_error_with_name(tn, msg));
                                    return obj;
                                }
                            }
                            // reject if trying to change value (SameValue per spec)
                            if (it2b(js_in(val_key_check, descriptor))) {
                                Item new_val = js_property_get(descriptor, val_key_check);
                                Item cur_val = js_property_get(obj, name);
                                if (!it2b(js_object_is(cur_val, new_val))) {
                                    Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                                    Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property: value"))};
                                    js_throw_value(js_new_error_with_name(tn, msg));
                                    return obj;
                                }
                            }
                        }
                    } else {
                        // 7e: accessor property — reject if get/set differ from current.
                        // Phase 4: prefer reading getter/setter from the JsAccessorPair
                        // stored at slot X (IS_ACCESSOR shape flag); fall back to legacy
                        // __get_/__set_ markers for objects still using the old scheme.
                        Item cur_pair_get = make_js_undefined();
                        Item cur_pair_set = make_js_undefined();
                        bool have_pair = false;
                        if (_se_acc_chk && jspd_is_accessor(_se_acc_chk)) {
                            Map* _m = (get_type_id(obj) == LMD_TYPE_MAP) ? obj.map :
                                      (get_type_id(obj) == LMD_TYPE_ARRAY && obj.array && obj.array->extra)
                                          ? (Map*)(uintptr_t)obj.array->extra : nullptr;
                            if (_m) {
                                bool sf = false;
                                Item sv = js_map_get_fast_ext(_m, ns_check->chars, (int)ns_check->len, &sf);
                                if (sf) {
                                    JsAccessorPair* pair = js_item_to_accessor_pair(sv);
                                    if (pair) {
                                        cur_pair_get = (pair->getter.item != ItemNull.item) ? pair->getter : make_js_undefined();
                                        cur_pair_set = (pair->setter.item != ItemNull.item) ? pair->setter : make_js_undefined();
                                        have_pair = true;
                                    }
                                }
                            }
                        }
                        if (it2b(js_in(get_key_check, descriptor))) {
                            Item new_get = js_property_get(descriptor, get_key_check);
                            // AT-3: post-AT-1 accessors always go through IS_ACCESSOR
                            // shape probe; the !have_pair branch is unreachable.
                            Item cur_get = have_pair ? cur_pair_get : make_js_undefined();
                            if (!it2b(js_object_is(cur_get, new_get))) {
                                Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                                Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property: getter"))};
                                js_throw_value(js_new_error_with_name(tn, msg));
                                return obj;
                            }
                        }
                        if (it2b(js_in(set_key_check, descriptor))) {
                            Item new_set = js_property_get(descriptor, set_key_check);
                            Item cur_set = have_pair ? cur_pair_set : make_js_undefined();
                            if (!it2b(js_object_is(cur_set, new_set))) {
                                Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                                Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property: setter"))};
                                js_throw_value(js_new_error_with_name(tn, msg));
                                return obj;
                            }
                        }
                    }
                }
            }
        }
    }

    // Stage A2.4: route storage through unified kernel
    // `js_define_own_property_from_descriptor` (in js_props.cpp).
    //
    // The kernel performs all storage writes:
    //   - Accessor: install via the IS_ACCESSOR chokepoint (`js_define_accessor_partial`).
    //               Tombstones data slot when converting data→accessor.
    //               Conditionally clears stale __nw_ marker.
    //   - Data: clears IS_ACCESSOR shape flag if previously accessor;
    //           tombstones legacy __get_/__set_ markers; tracks was_accessor.
    //   - Attribute markers __nw_/__nc_/__ne_ written from HAS_* bits;
    //     new-property defaults to non-* (ES §6.2.5.5).
    //   - For new data property OR accessor→data conversion without explicit
    //     `writable`, default to non-writable.
    //
    // What stays inline here:
    //   - Generic-descriptor undefined-write fallback (no value/get/set).
    //   - Array index hole-fill for accessor descriptors at sparse indices.

    Item nm = js_to_string(name);
    if (get_type_id(nm) != LMD_TYPE_STRING) return obj;
    String* nm_s = it2s(nm);
    if (!nm_s || nm_s->len >= 200) return obj;
    const char* nm_chars = nm_s->chars;
    int nm_len = (int)nm_s->len;

    JsPropertyDescriptor pd;
    if (!js_descriptor_from_object(descriptor, &pd)) return obj;

    bool is_accessor = js_pd_is_accessor(&pd);
    bool is_data = (pd.flags & JS_PD_HAS_VALUE) != 0;

    js_define_own_property_from_descriptor(obj, nm_chars, nm_len, &pd, is_new_property);

    // ES5 §15.4.5.1: array exotic — accessor at sparse index grows items array.
    if (is_accessor && get_type_id(obj) == LMD_TYPE_ARRAY && nm_len > 0 && nm_len <= 10) {
        bool is_idx = true;
        int64_t idx = 0;
        for (int ci = 0; ci < nm_len; ci++) {
            char ch = nm_chars[ci];
            if (ch < '0' || ch > '9') { is_idx = false; break; }
            idx = idx * 10 + (ch - '0');
        }
        if (is_idx && (nm_len == 1 || nm_chars[0] != '0') && idx >= 0) {
            Array* arr = obj.array;
            int64_t gap = idx - (int64_t)arr->length;
            if (gap >= 0 && gap < 100000) {
                extern void js_array_push_item_direct(Array* arr, Item value);
                Item hole = (Item){.item = JS_DELETED_SENTINEL_VAL};
                while ((int64_t)arr->length <= idx) {
                    js_array_push_item_direct(arr, hole);
                }
            }
        }
    }
    return obj;
}

// v24: strict mode flag from js_runtime.cpp
extern bool js_strict_mode;

// forward declarations
static bool js_is_symbol_item(Item item);

#ifdef __APPLE__
#include <mach/mach_time.h>
#include <mach/mach.h>
#include <mach/task_info.h>
#endif

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

extern void* heap_alloc(int size, TypeId type_id);
extern void fn_array_set(Array* arr, int64_t index, Item value);
extern "C" void js_set_prototype(Item object, Item prototype);
extern "C" Item js_get_prototype(Item object);
extern Item _map_read_field(ShapeEntry* field, void* map_data);
extern Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
extern "C" Item js_object_get_own_property_names(Item object);
extern "C" Item js_object_get_own_property_symbols(Item object);
extern "C" Item js_array_push(Item array, Item value);
extern "C" Item js_object_define_property(Item obj, Item name, Item descriptor);
extern "C" Item js_object_prevent_extensions(Item obj);
extern "C" Item js_get_generator_shared_proto(bool is_async);

// forward declaration for builtin method check helper
static bool js_map_has_builtin_method(Map* m, const char* name, int len);
extern "C" Item js_lookup_builtin_method(TypeId type, const char* name, int len);

// v18l: helper to throw TypeError if argument is not an object (ES5 §15.2.3.*)
static bool js_require_object_type(Item arg, const char* method_name) {
    TypeId t = get_type_id(arg);
    if (t == LMD_TYPE_MAP || t == LMD_TYPE_ARRAY || t == LMD_TYPE_FUNC || t == LMD_TYPE_ELEMENT)
        return true;
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);
    Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
    char msg[128];
    snprintf(msg, sizeof(msg), "Object.%s called on non-object", method_name);
    Item msg_item = (Item){.item = s2it(heap_create_name(msg, strlen(msg)))};
    Item error = js_new_error_with_name(type_name, msg_item);
    js_throw_value(error);
    return false;
}

// =============================================================================
// Process I/O
// =============================================================================

extern "C" Item js_process_stdout_write(Item str_item) {
    TypeId type = get_type_id(str_item);
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(str_item);
        if (s && s->len > 0) {
            fwrite(s->chars, 1, s->len, stdout);
            fflush(stdout);
        }
    } else {
        // Convert to string first
        Item str = js_to_string(str_item);
        String* s = it2s(str);
        if (s && s->len > 0) {
            fwrite(s->chars, 1, s->len, stdout);
            fflush(stdout);
        }
    }
    return (Item){.item = ITEM_TRUE};
}

// process.stderr.write(string) — writes to stderr (fd 2)
extern "C" Item js_process_stderr_write(Item str_item) {
    TypeId type = get_type_id(str_item);
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(str_item);
        if (s && s->len > 0) {
            fwrite(s->chars, 1, s->len, stderr);
            fflush(stderr);
        }
    } else {
        Item str = js_to_string(str_item);
        String* s = it2s(str);
        if (s && s->len > 0) {
            fwrite(s->chars, 1, s->len, stderr);
            fflush(stderr);
        }
    }
    return (Item){.item = ITEM_TRUE};
}

// process.stdin.read() — read a line from stdin
extern "C" Item js_process_stdin_read(void) {
    char buf[4096];
    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        return ItemNull;
    }
    int len = (int)strlen(buf);
    String* s = heap_create_name(buf, (size_t)len);
    return (Item){.item = s2it(s)};
}

extern "C" Item js_process_hrtime_bigint(void) {
    // Return nanosecond-precision monotonic time as BigInt (per Node.js spec)
    extern Item bigint_from_string(const char* str, int len);
    uint64_t ns;
#ifdef __APPLE__
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    uint64_t ticks = mach_absolute_time();
    ns = ticks * timebase.numer / timebase.denom;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)ns);
    return bigint_from_string(buf, (int)strlen(buf));
}

// process.hrtime([prev]) — returns [seconds, nanoseconds]
// If prev is given, returns difference from prev
extern "C" Item js_process_hrtime(Item prev) {
#ifdef __APPLE__
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    uint64_t ticks = mach_absolute_time();
    double ns = (double)ticks * (double)timebase.numer / (double)timebase.denom;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double ns = (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
#endif
    // If prev is an array [sec, nsec], subtract it
    if (get_type_id(prev) == LMD_TYPE_ARRAY) {
        Item prev_sec_item = js_array_get_int(prev, 0);
        Item prev_nsec_item = js_array_get_int(prev, 1);
        int64_t prev_sec_val = (get_type_id(prev_sec_item) == LMD_TYPE_INT) ? it2i(prev_sec_item) : 0;
        int64_t prev_nsec_val = (get_type_id(prev_nsec_item) == LMD_TYPE_INT) ? it2i(prev_nsec_item) : 0;
        double prev_ns = (double)prev_sec_val * 1e9 + (double)prev_nsec_val;
        ns -= prev_ns;
    }
    uint64_t total_ns = (uint64_t)ns;
    uint64_t sec = total_ns / 1000000000ULL;
    uint64_t nsec = total_ns % 1000000000ULL;
    Item arr = js_array_new(0);
    js_array_push(arr, (Item){.item = i2it((int64_t)sec)});
    js_array_push(arr, (Item){.item = i2it((int64_t)nsec)});
    return arr;
}

// performance.now() — returns milliseconds (monotonic, high-resolution)
// Uses static buffer (not GC heap) because the returned float must survive GC cycles.
// MIR-generated code stores the Item value in registers/stack, where the conservative
// GC scanner may not find it (nursery floats get overwritten; heap floats get collected).
static double js_perf_now_buf[64];
static int js_perf_now_idx = 0;

extern "C" Item js_performance_now(void) {
#ifdef __APPLE__
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    uint64_t ticks = mach_absolute_time();
    double ns = (double)ticks * (double)timebase.numer / (double)timebase.denom;
    double ms = ns / 1e6;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
    double* fp = &js_perf_now_buf[js_perf_now_idx % 64];
    js_perf_now_idx++;
    *fp = ms;
    return (Item){.item = d2it(fp)};
}

// Date.now() — returns milliseconds since Unix epoch
static double js_date_now_buf[64];
static int js_date_now_idx = 0;

extern "C" Item js_date_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    double ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = ms;
    return (Item){.item = d2it(fp)};
}

// new Date() — returns a map that acts as a Date object.
// Stores the current timestamp so .getTime() can retrieve it at runtime.
// The transpiler handles new Date().getTime() as a special case (→ js_date_now()),
// but js_date_new() is needed if the Date object is stored in a variable first.
extern "C" Item js_date_new(void) {
    Item obj = js_new_object();
    Item time_val = js_date_now();
    Item key = (Item){.item = s2it(heap_create_name("__time__"))};
    js_property_set(obj, key, time_val);
    // mark as Date for instanceof
    Item cls_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    js_property_set(obj, cls_key, (Item){.item = s2it(heap_create_name("Date"))});
    js_class_stamp(obj, JS_CLASS_DATE);  // A3-T3b
    return obj;
}

// Date() without 'new' — returns a string representation of the current date/time
extern "C" Item js_date_now_string(void) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char buf[128];
    // Match V8/SpiderMonkey format: "Thu Jan 01 1970 00:00:00 GMT+0000 (Timezone)"
    strftime(buf, sizeof(buf), "%a %b %d %Y %H:%M:%S GMT%z", tm_info);
    return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
}

// new Date(value) — accepts a numeric timestamp (ms since epoch) or a date string
extern "C" Item js_date_new_from(Item value) {
    Item obj = js_new_object();
    Item key = (Item){.item = s2it(heap_create_name("__time__"))};
    TypeId tid = get_type_id(value);

    // helper: store ms with TimeClip validation (|v| > 8.64e15 → NaN)
    auto store_time = [&](double ms) {
        if (isnan(ms) || isinf(ms) || ms > 8.64e15 || ms < -8.64e15) ms = NAN;
        static double date_buf[16];
        static int date_idx = 0;
        double* fp = &date_buf[date_idx++ % 16];
        *fp = ms;
        js_property_set(obj, key, (Item){.item = d2it(fp)});
    };

    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64 || tid == LMD_TYPE_FLOAT) {
        double ms;
        if (tid == LMD_TYPE_FLOAT) ms = it2d(value);
        else ms = (double)it2i(value);
        store_time(ms);
    } else if (tid == LMD_TYPE_STRING) {
        // parse date string — try ISO 8601 format
        String* s = it2s(value);
        // ES spec: extended year "-000000" is invalid (year zero must be "+000000")
        if (s && s->len >= 7 && memcmp(s->chars, "-000000", 7) == 0) {
            store_time(NAN);
        } else if (s) {
            struct tm tm = {};
            char* rest = strptime(s->chars, "%Y-%m-%dT%H:%M:%S", &tm);
            if (!rest) rest = strptime(s->chars, "%a %b %d %Y %H:%M:%S", &tm);
            if (!rest) rest = strptime(s->chars, "%c", &tm);
            if (rest) {
                time_t t = timegm(&tm);
                double ms = (double)t * 1000.0;
                // Parse fractional seconds (e.g. ".872" → 872ms)
                if (rest && *rest == '.') {
                    char* end_frac;
                    double frac = strtod(rest, &end_frac);
                    ms += frac * 1000.0;
                    rest = end_frac;
                }
                store_time(ms);
            } else {
                // fallback: try mktime (local time)
                struct tm tm2 = {};
                if (sscanf(s->chars, "%d-%d-%d", &tm2.tm_year, &tm2.tm_mon, &tm2.tm_mday) == 3) {
                    tm2.tm_year -= 1900;
                    tm2.tm_mon -= 1;
                    time_t t = mktime(&tm2);
                    double ms = (double)t * 1000.0;
                    store_time(ms);
                } else {
                    // If unparseable, set NaN (Invalid Date)
                    store_time(NAN);
                }
            }
        } else {
            store_time(NAN);
        }
    } else if (tid == LMD_TYPE_MAP) {
        // Date object: extract _time from the other Date
        bool has_time = false;
        Item other_time = js_map_get_fast_ext(value.map, "__time__", 8, &has_time);
        if (has_time && (get_type_id(other_time) == LMD_TYPE_FLOAT || get_type_id(other_time) == LMD_TYPE_INT || get_type_id(other_time) == LMD_TYPE_INT64)) {
            double ms = (get_type_id(other_time) == LMD_TYPE_FLOAT) ? it2d(other_time) : (double)it2i(other_time);
            store_time(ms);
        } else {
            // Non-Date object: ToPrimitive(value, default) per ES spec §21.4.2.
            // J39-1b: route through unified js_to_primitive (ES §7.1.1).
            Item prim = js_to_primitive(value, JS_HINT_DEFAULT);
            if (js_check_exception()) return ItemNull;
            TypeId pt = get_type_id(prim);
            // Symbol results → throw TypeError
            if ((pt == LMD_TYPE_INT && it2i(prim) <= -(int64_t)JS_SYMBOL_BASE) || pt == LMD_TYPE_SYMBOL) {
                js_throw_type_error("Cannot convert a Symbol value to a number");
                return ItemNull;
            }
            // Dispatch on ToPrimitive result type
            if (pt == LMD_TYPE_STRING) {
                // Re-enter Date constructor with the string
                return js_date_new_from(prim);
            } else {
                // ToNumber on the primitive
                Item num = js_to_number(prim);
                if (js_check_exception()) return ItemNull;
                TypeId nt = get_type_id(num);
                if (nt == LMD_TYPE_FLOAT)
                    store_time(it2d(num));
                else if (nt == LMD_TYPE_INT || nt == LMD_TYPE_INT64)
                    store_time((double)it2i(num));
                else
                    store_time(NAN);
            }
        }
    } else {
        // Per spec: ToNumber(value) then TimeClip
        // null→0, undefined→NaN, true→1, false→0
        Item num = js_to_number(value);
        TypeId nt = get_type_id(num);
        if (nt == LMD_TYPE_FLOAT) store_time(it2d(num));
        else if (nt == LMD_TYPE_INT || nt == LMD_TYPE_INT64) store_time((double)it2i(num));
        else store_time(NAN);
    }
date_done:
    Item cls_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    js_property_set(obj, cls_key, (Item){.item = s2it(heap_create_name("Date"))});
    return obj;
}

// Date.UTC(year, month[, day[, hour[, min[, sec[, ms]]]]]) — returns ms since epoch
extern "C" Item js_date_utc(Item args_array) {
    // args_array is a JS array
    int len = (int)js_array_length(args_array);
    int year = 0, month = 0, day = 1, hour = 0, min = 0, sec = 0, millis = 0;
    auto get_arg_int = [&](int idx) -> int {
        Item val = js_array_get_int(args_array, idx);
        TypeId t = get_type_id(val);
        if (t == LMD_TYPE_INT) return (int)it2i(val);
        if (t == LMD_TYPE_FLOAT) return (int)it2d(val);
        return 0;
    };
    if (len > 0) year = get_arg_int(0);
    if (len > 1) month = get_arg_int(1);
    if (len > 2) day = get_arg_int(2);
    if (len > 3) hour = get_arg_int(3);
    if (len > 4) min = get_arg_int(4);
    if (len > 5) sec = get_arg_int(5);
    if (len > 6) millis = get_arg_int(6);

    struct tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month;       // 0-based
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;

    time_t t = timegm(&tm);
    double ms = (double)t * 1000.0 + (double)millis;
    static double utc_buf[16];
    static int utc_idx = 0;
    double* fp = &utc_buf[utc_idx++ % 16];
    *fp = ms;
    return (Item){.item = d2it(fp)};
}

// v11: Date instance method dispatch
// method_id: 0=getTime, 1=getFullYear, 2=getMonth, 3=getDate,
//   4=getHours, 5=getMinutes, 6=getSeconds, 7=getMilliseconds,
//   8=toISOString, 9=toLocaleDateString
extern "C" Item js_date_method(Item date_obj, int method_id) {
    // extract epoch-ms from the _time property
    Item key = (Item){.item = s2it(heap_create_name("__time__"))};
    Item time_val = js_property_get(date_obj, key);

    // guard: if no _time property, receiver is not a Date object — TypeError per ES spec
    TypeId tv_type = get_type_id(time_val);
    if (tv_type != LMD_TYPE_FLOAT && tv_type != LMD_TYPE_INT && tv_type != LMD_TYPE_INT64) {
        // The transpiler routes .toISOString() here unconditionally;
        // non-Date objects may have their own methods via prototype chain.
        if (method_id == 8) { // toISOString
            Item mk = (Item){.item = s2it(heap_create_name("toISOString", 11))};
            Item fn = js_property_access(date_obj, mk);
            if (get_type_id(fn) == LMD_TYPE_FUNC) {
                return js_call_function(fn, date_obj, nullptr, 0);
            }
        }
        return js_throw_type_error("this is not a Date object");
    }

    double ms = time_val.get_double();
    if (method_id == 0) { // getTime
        static double gt_buf[16];
        static int gt_idx = 0;
        double* fp = &gt_buf[gt_idx++ % 16];
        *fp = ms;
        return (Item){.item = d2it(fp)};
    }
    // NaN (Invalid Date) handling
    if (isnan(ms)) {
        if (method_id == 8) // toISOString: throw RangeError for Invalid Date
            return js_throw_type_error("Invalid time value");
        if (method_id == 17 || method_id == 9) // toString, toLocaleDateString
            return (Item){.item = s2it(heap_create_name("Invalid Date", 12))};
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }
    time_t secs = (time_t)(ms / 1000.0);
    struct tm tm;
    localtime_r(&secs, &tm);
    switch (method_id) {
        case 1: return (Item){.item = i2it(tm.tm_year + 1900)}; // getFullYear
        case 2: return (Item){.item = i2it(tm.tm_mon)};         // getMonth (0-based)
        case 3: return (Item){.item = i2it(tm.tm_mday)};        // getDate
        case 4: return (Item){.item = i2it(tm.tm_hour)};        // getHours
        case 5: return (Item){.item = i2it(tm.tm_min)};         // getMinutes
        case 6: return (Item){.item = i2it(tm.tm_sec)};         // getSeconds
        case 7: {                                                // getMilliseconds
            int millis = (int)(ms - (double)secs * 1000.0);
            return (Item){.item = i2it(millis)};
        }
        case 8: { // toISOString
            char buf[32];
            struct tm utc;
            gmtime_r(&secs, &utc);
            int millis = (int)(ms - (double)secs * 1000.0);
            snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                utc.tm_hour, utc.tm_min, utc.tm_sec, millis);
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case 9: { // toLocaleDateString
            char buf[32];
            snprintf(buf, sizeof(buf), "%d/%d/%04d",
                tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900);
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        default: break;
    }
    // UTC variants: use gmtime_r
    if (method_id >= 10 && method_id <= 16) {
        struct tm utc;
        gmtime_r(&secs, &utc);
        switch (method_id) {
            case 10: return (Item){.item = i2it(utc.tm_year + 1900)}; // getUTCFullYear
            case 11: return (Item){.item = i2it(utc.tm_mon)};         // getUTCMonth (0-based)
            case 12: return (Item){.item = i2it(utc.tm_mday)};        // getUTCDate
            case 13: return (Item){.item = i2it(utc.tm_hour)};        // getUTCHours
            case 14: return (Item){.item = i2it(utc.tm_min)};         // getUTCMinutes
            case 15: return (Item){.item = i2it(utc.tm_sec)};         // getUTCSeconds
            case 16: {                                                 // getUTCMilliseconds
                int millis = (int)(ms - (double)secs * 1000.0);
                return (Item){.item = i2it(millis)};
            }
            default: break;
        }
    }
    // toString: produce a human-readable date string parseable by new Date(str)
    if (method_id == 17) {
        struct tm utc;
        gmtime_r(&secs, &utc);
        // JS-style: "Thu Jun 09 3141 02:06:53 GMT+0000"
        static const char* wday[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* mon[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %s %02d %04d %02d:%02d:%02d GMT+0000",
            wday[utc.tm_wday], mon[utc.tm_mon], utc.tm_mday,
            utc.tm_year + 1900, utc.tm_hour, utc.tm_min, utc.tm_sec);
        return (Item){.item = s2it(heap_create_name(buf))};
    }
    return ItemNull;
}

// Process argv storage — C-level copy (safe before heap init)
static Item js_process_argv_items = {.item = ITEM_NULL};
static const char** js_process_argv_raw = NULL;
static int js_process_argc_raw = 0;

// v20: Date setter methods — mutate internal _time timestamp
// method_id: 20=setTime, 21=setFullYear, 22=setMonth, 23=setDate,
//   24=setHours, 25=setMinutes, 26=setSeconds, 27=setMilliseconds,
//   30=setUTCFullYear, 31=setUTCMonth, 32=setUTCDate,
//   33=setUTCHours, 34=setUTCMinutes, 35=setUTCSeconds, 36=setUTCMilliseconds
// 40=getDay, 41=getUTCDay, 42=getTimezoneOffset, 43=valueOf, 44=toJSON,
// 45=toUTCString, 46=toDateString, 47=toTimeString
extern "C" Item js_date_setter(Item date_obj, int method_id, Item arg0, Item arg1, Item arg2, Item arg3) {
    Item key = (Item){.item = s2it(heap_create_name("__time__"))};
    Item time_val = js_property_get(date_obj, key);

    // guard: if no _time property, receiver is not a Date object — TypeError per ES spec
    TypeId tv_type = get_type_id(time_val);
    if (tv_type != LMD_TYPE_FLOAT && tv_type != LMD_TYPE_INT && tv_type != LMD_TYPE_INT64) {
        if (method_id == 43) { // valueOf — non-Date: check own/prototype valueOf first
            // The transpiler unconditionally routes *.valueOf() here, so we must
            // handle non-Date objects by looking up their own valueOf function.
            Item vo_key = (Item){.item = s2it(heap_create_name("valueOf", 7))};
            Item fn = js_property_access(date_obj, vo_key);
            if (get_type_id(fn) == LMD_TYPE_FUNC) {
                return js_call_function(fn, date_obj, nullptr, 0);
            }
            return date_obj;
        }
        if (method_id == 44) { // toJSON — non-Date: check own/prototype toJSON first
            Item tj_key = (Item){.item = s2it(heap_create_name("toJSON", 6))};
            Item fn = js_property_access(date_obj, tj_key);
            if (get_type_id(fn) == LMD_TYPE_FUNC) {
                return js_call_function(fn, date_obj, nullptr, 0);
            }
            return ItemNull;
        }
        return js_throw_type_error("this is not a Date object");
    }

    double ms = time_val.get_double();

    auto to_double = [](Item v) -> double {
        TypeId t = get_type_id(v);
        if (t == LMD_TYPE_FLOAT) return it2d(v);
        if (t == LMD_TYPE_INT || t == LMD_TYPE_INT64) return (double)it2i(v);
        return NAN;
    };
    auto is_present = [](Item v) -> bool {
        return v.item != ItemNull.item && get_type_id(v) != LMD_TYPE_UNDEFINED;
    };

    auto store_ms = [&](double new_ms) -> Item {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = new_ms;
        Item new_time = (Item){.item = d2it(fp)};
        js_property_set(date_obj, key, new_time);
        return new_time;
    };

    // getDay / getUTCDay / getTimezoneOffset / valueOf / toJSON / toUTCString / toDateString / toTimeString
    if (method_id >= 40) {
        // NaN (Invalid Date) handling
        if (isnan(ms)) {
            if (method_id == 43) { // valueOf — return NaN
                double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
                *fp = NAN;
                return (Item){.item = d2it(fp)};
            }
            if (method_id == 44) // toJSON — return null for Invalid Date
                return ItemNull;
            if (method_id == 45 || method_id == 46 || method_id == 47) // string representations
                return (Item){.item = s2it(heap_create_name("Invalid Date", 12))};
            double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *fp = NAN;
            return (Item){.item = d2it(fp)};
        }
        time_t secs = (time_t)(ms / 1000.0);
        if (method_id == 40) { // getDay
            struct tm tm; localtime_r(&secs, &tm);
            return (Item){.item = i2it(tm.tm_wday)};
        }
        if (method_id == 41) { // getUTCDay
            struct tm utc; gmtime_r(&secs, &utc);
            return (Item){.item = i2it(utc.tm_wday)};
        }
        if (method_id == 42) { // getTimezoneOffset
            struct tm local_tm; localtime_r(&secs, &local_tm);
            // tm_gmtoff is seconds east of UTC; getTimezoneOffset returns minutes west of UTC
            int offset_min = -(int)(local_tm.tm_gmtoff / 60);
            return (Item){.item = i2it(offset_min)};
        }
        if (method_id == 43) { // valueOf — same as getTime
            double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *fp = ms;
            return (Item){.item = d2it(fp)};
        }
        if (method_id == 44) { // toJSON — same as toISOString
            return js_date_method(date_obj, 8);
        }
        if (method_id == 45) { // toUTCString
            struct tm utc; gmtime_r(&secs, &utc);
            static const char* wday[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
            static const char* mon[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
            char buf[64];
            snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d GMT",
                wday[utc.tm_wday], utc.tm_mday, mon[utc.tm_mon],
                utc.tm_year + 1900, utc.tm_hour, utc.tm_min, utc.tm_sec);
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        if (method_id == 46) { // toDateString
            struct tm tm; localtime_r(&secs, &tm);
            static const char* wday[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
            static const char* mon[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
            char buf[32];
            snprintf(buf, sizeof(buf), "%s %s %02d %04d",
                wday[tm.tm_wday], mon[tm.tm_mon], tm.tm_mday, tm.tm_year + 1900);
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        if (method_id == 47) { // toTimeString
            struct tm tm; localtime_r(&secs, &tm);
            long gmtoff = tm.tm_gmtoff;
            int h_off = (int)(gmtoff / 3600);
            int m_off = (int)((gmtoff % 3600) / 60);
            if (m_off < 0) m_off = -m_off;
            char buf[64];
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d GMT%+03d%02d",
                tm.tm_hour, tm.tm_min, tm.tm_sec, h_off, m_off);
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        if (method_id == 50) { // getYear — Annex B: returns year - 1900
            struct tm tm; localtime_r(&secs, &tm);
            return (Item){.item = i2it(tm.tm_year)}; // tm_year is already year - 1900
        }
        if (method_id == 51) { // setYear — Annex B
            // ES spec: ToNumber(symbol) throws TypeError
            if (get_type_id(arg0) == LMD_TYPE_INT && it2i(arg0) <= -(int64_t)JS_SYMBOL_BASE) {
                extern Item js_throw_type_error(const char* msg);
                return js_throw_type_error("Cannot convert a Symbol value to a number");
            }
            Item num = js_to_number(arg0);
            if (js_check_exception()) return ItemNull;
            double y = to_double(num);
            if (isnan(y)) return store_ms(NAN);
            int iy = (int)y;
            // ES Annex B §B.2.4.1: if 0 ≤ y ≤ 99, year = y + 1900
            if (iy >= 0 && iy <= 99) iy += 1900;
            // ES Annex B §B.2.4.2 step 2: if t is NaN, let t be +0
            double base_ms = isnan(ms) ? 0.0 : ms;
            time_t base_secs = (time_t)(base_ms / 1000.0);
            int old_millis = (int)(base_ms - (double)base_secs * 1000.0);
            if (old_millis < 0) old_millis += 1000;
            struct tm tm; localtime_r(&base_secs, &tm);
            tm.tm_year = iy - 1900;
            tm.tm_isdst = -1;
            time_t new_secs = mktime(&tm);
            double new_ms = (double)new_secs * 1000.0 + old_millis;
            return store_ms(new_ms);
        }
        return ItemNull;
    }

    if (method_id == 20) { // setTime
        Item num = js_to_number(arg0);
        if (js_check_exception()) return ItemNull;
        double new_ms = to_double(num);
        return store_ms(new_ms);
    }

    // Date setters (methods 21-36): local (21-27) and UTC (30-36)
    // ES spec: call ToNumber on all present arguments first (left-to-right, for side effects),
    // then check NaN on date/args, then compute. This ensures valueOf/toString side effects
    // and Symbol TypeError throws happen in the correct order.
    if (method_id >= 21 && method_id <= 36) {
        Item n0 = js_to_number(arg0);
        if (js_check_exception()) return ItemNull;
        double v0 = to_double(n0);

        double v1 = NAN;
        if (is_present(arg1)) {
            Item n1 = js_to_number(arg1);
            if (js_check_exception()) return ItemNull;
            v1 = to_double(n1);
        }
        double v2 = NAN;
        if (is_present(arg2)) {
            Item n2 = js_to_number(arg2);
            if (js_check_exception()) return ItemNull;
            v2 = to_double(n2);
        }
        double v3 = NAN;
        if (is_present(arg3)) {
            Item n3 = js_to_number(arg3);
            if (js_check_exception()) return ItemNull;
            v3 = to_double(n3);
        }

        // ES spec: if any required arg is NaN, result is NaN
        if (isnan(v0)) return store_ms(NAN);
        if (is_present(arg1) && isnan(v1)) return store_ms(NAN);
        if (is_present(arg2) && isnan(v2)) return store_ms(NAN);
        if (is_present(arg3) && isnan(v3)) return store_ms(NAN);

        // ES spec: setFullYear/setUTCFullYear — if date is NaN, use +0
        if ((method_id == 21 || method_id == 30) && isnan(ms)) ms = 0.0;
        // all other setters: if t (original [[DateValue]]) is NaN, return NaN without writing
        // (ToNumber args may have side effects that change the date, per spec step 8 check uses t)
        if (isnan(ms)) {
            double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *fp = NAN;
            return (Item){.item = d2it(fp)};
        }

        // local setters (21-27)
        if (method_id >= 21 && method_id <= 27) {
            time_t secs = (time_t)(ms / 1000.0);
            int old_millis = (int)(ms - (double)secs * 1000.0);
            if (old_millis < 0) old_millis += 1000;
            struct tm tm;
            localtime_r(&secs, &tm);

            switch (method_id) {
                case 21: // setFullYear(year [, month, date])
                    tm.tm_year = (int)v0 - 1900;
                    if (is_present(arg1)) tm.tm_mon = (int)v1;
                    if (is_present(arg2)) tm.tm_mday = (int)v2;
                    break;
                case 22: // setMonth(month [, date])
                    tm.tm_mon = (int)v0;
                    if (is_present(arg1)) tm.tm_mday = (int)v1;
                    break;
                case 23: // setDate(date)
                    tm.tm_mday = (int)v0;
                    break;
                case 24: // setHours(hour [, min, sec, ms])
                    tm.tm_hour = (int)v0;
                    if (is_present(arg1)) tm.tm_min = (int)v1;
                    if (is_present(arg2)) tm.tm_sec = (int)v2;
                    if (is_present(arg3)) old_millis = (int)v3;
                    break;
                case 25: // setMinutes(min [, sec, ms])
                    tm.tm_min = (int)v0;
                    if (is_present(arg1)) tm.tm_sec = (int)v1;
                    if (is_present(arg2)) old_millis = (int)v2;
                    break;
                case 26: // setSeconds(sec [, ms])
                    tm.tm_sec = (int)v0;
                    if (is_present(arg1)) old_millis = (int)v1;
                    break;
                case 27: // setMilliseconds(ms)
                    old_millis = (int)v0;
                    break;
            }
            tm.tm_isdst = -1;
            time_t new_secs = mktime(&tm);
            double new_ms = (double)new_secs * 1000.0 + (double)old_millis;
            return store_ms(new_ms);
        }

        // UTC setters (30-36)
        if (method_id >= 30 && method_id <= 36) {
            time_t secs = (time_t)(ms / 1000.0);
            int old_millis = (int)(ms - (double)secs * 1000.0);
            if (old_millis < 0) old_millis += 1000;
            struct tm utc;
            gmtime_r(&secs, &utc);

            switch (method_id) {
                case 30: // setUTCFullYear(year [, month, date])
                    utc.tm_year = (int)v0 - 1900;
                    if (is_present(arg1)) utc.tm_mon = (int)v1;
                    if (is_present(arg2)) utc.tm_mday = (int)v2;
                    break;
                case 31: // setUTCMonth(month [, date])
                    utc.tm_mon = (int)v0;
                    if (is_present(arg1)) utc.tm_mday = (int)v1;
                    break;
                case 32: // setUTCDate(date)
                    utc.tm_mday = (int)v0;
                    break;
                case 33: // setUTCHours(hour [, min, sec, ms])
                    utc.tm_hour = (int)v0;
                    if (is_present(arg1)) utc.tm_min = (int)v1;
                    if (is_present(arg2)) utc.tm_sec = (int)v2;
                    if (is_present(arg3)) old_millis = (int)v3;
                    break;
                case 34: // setUTCMinutes(min [, sec, ms])
                    utc.tm_min = (int)v0;
                    if (is_present(arg1)) utc.tm_sec = (int)v1;
                    if (is_present(arg2)) old_millis = (int)v2;
                    break;
                case 35: // setUTCSeconds(sec [, ms])
                    utc.tm_sec = (int)v0;
                    if (is_present(arg1)) old_millis = (int)v1;
                    break;
                case 36: // setUTCMilliseconds(ms)
                    old_millis = (int)v0;
                    break;
            }
            time_t new_secs = timegm(&utc);
            double new_ms = (double)new_secs * 1000.0 + (double)old_millis;
            return store_ms(new_ms);
        }
    }

    return ItemNull;
}

// v20: new Date(year, month [, day, hours, minutes, seconds, ms]) — multi-arg constructor
// ES §21.4.2.1 step 3: Call ToNumber on each present argument in left-to-right order
// (for side effects), THEN check NaN, THEN compute final time. Exceptions from any
// ToNumber must propagate immediately and halt remaining coercions.
extern "C" Item js_date_new_multi(Item args_array) {
    int len = (int)js_array_length(args_array);

    auto coerce = [&](int idx, bool* present_out) -> double {
        if (idx >= len) { *present_out = false; return 0.0; }
        Item val = js_array_get_int(args_array, idx);
        // ES treats only "missing" as not-present. Explicit `undefined` is present (and ToNumber→NaN).
        // js_array_get_int returns ItemNull (== {item=0}) for out-of-bounds; here idx<len so it's
        // a real element. But Lambda's array storage may give back undefined for holes — accept that.
        *present_out = true;
        Item num = js_to_number(val);
        if (js_check_exception()) return 0.0;
        TypeId t = get_type_id(num);
        if (t == LMD_TYPE_FLOAT) return it2d(num);
        if (t == LMD_TYPE_INT || t == LMD_TYPE_INT64) return (double)it2i(num);
        return NAN;
    };

    bool p_year=false, p_month=false, p_day=false, p_hour=false, p_min=false, p_sec=false, p_ms=false;
    double y = coerce(0, &p_year);  if (js_check_exception()) return ItemNull;
    double m = coerce(1, &p_month); if (js_check_exception()) return ItemNull;
    double d = coerce(2, &p_day);   if (js_check_exception()) return ItemNull;
    double h = coerce(3, &p_hour);  if (js_check_exception()) return ItemNull;
    double mi = coerce(4, &p_min);  if (js_check_exception()) return ItemNull;
    double s = coerce(5, &p_sec);   if (js_check_exception()) return ItemNull;
    double ms = coerce(6, &p_ms);   if (js_check_exception()) return ItemNull;

    // ES spec: defaults for unsupplied args
    if (!p_day) d = 1;
    if (!p_hour) h = 0;
    if (!p_min) mi = 0;
    if (!p_sec) s = 0;
    if (!p_ms) ms = 0;

    // Build the Date object now (so we always return a Date even when time value is NaN)
    Item obj = js_new_object();
    Item time_key = (Item){.item = s2it(heap_create_name("__time__"))};
    Item cls_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    js_property_set(obj, cls_key, (Item){.item = s2it(heap_create_name("Date"))});

    // ES spec: if any of y, m, d, h, mi, s, ms is NaN → final time value is NaN
    double ms_val;
    if (isnan(y) || isnan(m) || isnan(d) || isnan(h) || isnan(mi) || isnan(s) || isnan(ms)) {
        ms_val = NAN;
    } else {
        // ES spec: if 0 <= ToInteger(y) <= 99, year = 1900 + ToInteger(y)
        int iy = (int)y;
        if (iy >= 0 && iy <= 99) iy += 1900;

        struct tm tm = {};
        tm.tm_year = iy - 1900;
        tm.tm_mon = (int)m;
        tm.tm_mday = (int)d;
        tm.tm_hour = (int)h;
        tm.tm_min = (int)mi;
        tm.tm_sec = (int)s;
        tm.tm_isdst = -1;
        time_t t = mktime(&tm);  // local time constructor
        ms_val = (double)t * 1000.0 + ms;
    }
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = ms_val;
    js_property_set(obj, time_key, (Item){.item = d2it(fp)});
    return obj;
}

// v20: Date.parse(string) — parse a date string, return ms since epoch
extern "C" Item js_date_parse(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }
    // ES spec: extended year "-000000" is invalid (year zero must be "+000000")
    if (s->len >= 7 && memcmp(s->chars, "-000000", 7) == 0) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }
    struct tm tm = {};
    // Try ISO 8601 first
    if (strptime(s->chars, "%Y-%m-%dT%H:%M:%S", &tm) ||
        strptime(s->chars, "%Y-%m-%d", &tm) ||
        strptime(s->chars, "%a, %d %b %Y %H:%M:%S", &tm) ||
        strptime(s->chars, "%d %b %Y %H:%M:%S", &tm) ||
        strptime(s->chars, "%a %b %d %Y %H:%M:%S", &tm) ||
        strptime(s->chars, "%c", &tm)) {
        time_t t = timegm(&tm);
        double ms = (double)t * 1000.0;
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = ms;
        return (Item){.item = d2it(fp)};
    }
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = NAN;
    return (Item){.item = d2it(fp)};
}

extern "C" void js_store_process_argv(int argc, const char** argv) {
    // Store C-level copy — no heap allocation (safe before runtime context is ready)
    js_process_argc_raw = argc;
    js_process_argv_raw = argv;
}

extern "C" void js_set_process_argv(int argc, const char** argv) {
    // Build a Lambda array from the argv (requires heap to be active)
    Array* arr = array();
    for (int i = 0; i < argc; i++) {
        array_push(arr, (Item){.item = s2it(heap_create_name(argv[i]))});
    }
    js_process_argv_items = array_end(arr);
}

extern "C" Item js_get_process_argv(void) {
    // Lazy build: if raw argv was stored but Lambda array not yet built, build it now
    if (js_process_argv_items.item == ITEM_NULL && js_process_argc_raw > 0) {
        js_set_process_argv(js_process_argc_raw, js_process_argv_raw);
    }
    // Return an empty array if process.argv was never set (prevents null subscript crash)
    if (js_process_argv_items.item == ITEM_NULL) {
        Array* arr = array();
        js_process_argv_items = array_end(arr);
    }
    return js_process_argv_items;
}

// process object (lazy-initialized for `var p = process` standalone usage)
static Item js_process_object = {.item = ITEM_NULL};

// process.cwd()
extern "C" Item js_process_cwd(void) {
    char* cwd = file_getcwd();
    if (!cwd) return (Item){.item = s2it(heap_create_name(""))};
    Item result = (Item){.item = s2it(heap_create_name(cwd, strlen(cwd)))};
    mem_free(cwd);
    return result;
}

// process.chdir(directory)
extern "C" Item js_process_chdir(Item dir_item) {
    if (get_type_id(dir_item) != LMD_TYPE_STRING) return make_js_undefined();
    String* s = it2s(dir_item);
    char buf[2048];
    int len = (int)s->len;
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';
    if (chdir(buf) != 0) {
        log_error("process: chdir failed for '%s'", buf);
    }
    return make_js_undefined();
}

// process.exit([code])
static int js_process_exit_code_value = 0;
extern "C" void js_process_emit_exit(int code); // forward declaration

extern "C" Item js_process_exit(Item code_item) {
    int code = js_process_exit_code_value; // default to exitCode
    TypeId type = get_type_id(code_item);
    if (type == LMD_TYPE_INT) code = (int)it2i(code_item);
    else if (type == LMD_TYPE_FLOAT) code = (int)it2d(code_item);
    // Fire 'exit' listeners before terminating (Node.js compatibility)
    js_process_emit_exit(code);
    exit(code);
    return make_js_undefined(); // unreachable
}

// process.exitCode getter/setter
extern "C" Item js_process_get_exitCode(void) {
    return (Item){.item = i2it(js_process_exit_code_value)};
}

extern "C" Item js_process_set_exitCode(Item code_item) {
    TypeId type = get_type_id(code_item);
    if (type == LMD_TYPE_INT) js_process_exit_code_value = (int)it2i(code_item);
    else if (type == LMD_TYPE_FLOAT) js_process_exit_code_value = (int)it2d(code_item);
    return make_js_undefined();
}

// process.uptime()
extern "C" Item js_process_uptime(void) {
    static double start_time = 0;
    if (start_time == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        start_time = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = now - start_time;
    return (Item){.item = d2it(fp)};
}

// build process.env as a map of environment variables
static Item build_process_env(void) {
    Item env = js_new_object();
    // Mark as process.env so js_property_set coerces values to strings
    env.map->map_kind = MAP_KIND_PROCESS_ENV;
    extern char** environ;
    if (environ) {
        for (char** e = environ; *e; e++) {
            char* eq = strchr(*e, '=');
            if (eq) {
                Item key = (Item){.item = s2it(heap_create_name(*e, (size_t)(eq - *e)))};
                Item val = (Item){.item = s2it(heap_create_name(eq + 1, strlen(eq + 1)))};
                js_property_set(env, key, val);
            }
        }
    }
    // Skip Node.js flag-checking in common test module — Lambda doesn't support V8 flags
    js_property_set(env,
        (Item){.item = s2it(heap_create_name("NODE_SKIP_FLAG_CHECK", 20))},
        (Item){.item = s2it(heap_create_name("1", 1))});
    return env;
}

// build process.stdout object with write() method
static Item build_process_stdout(void) {
    Item stdout_obj = js_new_object();
    Item write_fn = js_new_function((void*)js_process_stdout_write, 1);
    js_property_set(stdout_obj, (Item){.item = s2it(heap_create_name("write", 5))}, write_fn);
    js_property_set(stdout_obj, (Item){.item = s2it(heap_create_name("fd", 2))}, (Item){.item = i2it(1)});
    js_property_set(stdout_obj, (Item){.item = s2it(heap_create_name("isTTY", 5))},
        (Item){.item = b2it(isatty(1))});
    return stdout_obj;
}

// build process.stderr object with write() method
static Item build_process_stderr(void) {
    Item stderr_obj = js_new_object();
    Item write_fn = js_new_function((void*)js_process_stderr_write, 1);
    js_property_set(stderr_obj, (Item){.item = s2it(heap_create_name("write", 5))}, write_fn);
    js_property_set(stderr_obj, (Item){.item = s2it(heap_create_name("fd", 2))}, (Item){.item = i2it(2)});
    js_property_set(stderr_obj, (Item){.item = s2it(heap_create_name("isTTY", 5))},
        (Item){.item = b2it(isatty(2))});
    return stderr_obj;
}

// build process.stdin object with read() method and basic Readable-like interface
static Item build_process_stdin(void) {
    Item stdin_obj = js_new_object();
    Item read_fn = js_new_function((void*)js_process_stdin_read, 0);
    js_property_set(stdin_obj, (Item){.item = s2it(heap_create_name("read", 4))}, read_fn);
    js_property_set(stdin_obj, (Item){.item = s2it(heap_create_name("fd", 2))}, (Item){.item = i2it(0)});
    js_property_set(stdin_obj, (Item){.item = s2it(heap_create_name("isTTY", 5))},
        (Item){.item = b2it(isatty(0))});
    // setEncoding — stub that just records encoding
    // resume/pause — stubs for stream interface
    return stdin_obj;
}

// process.nextTick(callback, ...args) — queue callback before microtasks
extern "C" Item js_process_nextTick(Item rest_args) {
    int argc = js_array_length(rest_args);
    if (argc == 0) return make_js_undefined();
    Item callback = js_array_get_int(rest_args, 0);
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_type_error("The \"callback\" argument must be of type function");
    }
    extern void js_microtask_enqueue(Item func);
    if (argc == 1) {
        // no extra args — enqueue callback directly
        js_microtask_enqueue(callback);
    } else {
        // bind extra args: callback.bind(undefined, arg1, arg2, ...)
        extern Item js_bind_function(Item func, Item this_val, Item* args, int arg_count);
        int extra = argc - 1;
        Item* bound_args = (Item*)alloca(extra * sizeof(Item));
        for (int i = 0; i < extra; i++) {
            bound_args[i] = js_array_get_int(rest_args, i + 1);
        }
        Item bound = js_bind_function(callback, make_js_undefined(), bound_args, extra);
        js_microtask_enqueue(bound);
    }
    return make_js_undefined();
}

// process.binding(name) — deprecated, returns empty objects or specific bindings
extern "C" Item js_process_binding(Item name) {
    if (get_type_id(name) != LMD_TYPE_STRING) return js_new_object();
    String* s = it2s(name);
    // process.binding('natives') — return empty object (tests check it exists)
    // process.binding('config') — return config object
    if (s->len == 6 && memcmp(s->chars, "config", 6) == 0) {
        Item cfg = js_new_object();
        js_property_set(cfg, (Item){.item = s2it(heap_create_name("hasOpenSSL", 10))}, (Item){.item = ITEM_TRUE});
        js_property_set(cfg, (Item){.item = s2it(heap_create_name("hasCrypto", 9))}, (Item){.item = ITEM_TRUE});
        js_property_set(cfg, (Item){.item = s2it(heap_create_name("fipsMode", 8))}, (Item){.item = ITEM_FALSE});
        return cfg;
    }
    return js_new_object();
}

// process.dlopen(module, filename) — stub for native addon loading
extern "C" Item js_process_dlopen(Item module, Item filename) {
    return js_throw_type_error_code("ERR_DLOPEN_FAILED",
        "process.dlopen is not supported in Lambda");
}

// Set.has() stub — always returns false (for allowedNodeEnvironmentFlags)
extern "C" Item js_set_has_stub(Item self, Item key) {
    (void)self;
    (void)key;
    return (Item){.item = ITEM_FALSE};
}

// process.report.getReport() — return minimal diagnostic report
extern "C" Item js_process_report_getReport(void) {
    Item report = js_new_object();
    Item header = js_new_object();
    js_property_set(header,
        (Item){.item = s2it(heap_create_name("nodeVersion", 11))},
        (Item){.item = s2it(heap_create_name("v20.0.0", 7))});
    js_property_set(header,
        (Item){.item = s2it(heap_create_name("platform", 8))},
#ifdef __APPLE__
        (Item){.item = s2it(heap_create_name("darwin", 6))});
#elif defined(__linux__)
        (Item){.item = s2it(heap_create_name("linux", 5))});
#else
        (Item){.item = s2it(heap_create_name("win32", 5))});
#endif
    js_property_set(report,
        (Item){.item = s2it(heap_create_name("header", 6))},
        header);
    return report;
}

#ifndef _WIN32
#include <unistd.h>
#include <grp.h>
// process.setuid(uid)
extern "C" Item js_process_setuid(Item uid_item) {
    if (get_type_id(uid_item) == LMD_TYPE_INT) {
        int r = setuid((uid_t)it2i(uid_item));
        if (r != 0) {
            return js_throw_error_with_code("ERR_UNKNOWN_CREDENTIAL",
                "setuid failed");
        }
    }
    return make_js_undefined();
}

// process.setgid(gid)
extern "C" Item js_process_setgid(Item gid_item) {
    if (get_type_id(gid_item) == LMD_TYPE_INT) {
        int r = setgid((gid_t)it2i(gid_item));
        if (r != 0) {
            return js_throw_error_with_code("ERR_UNKNOWN_CREDENTIAL",
                "setgid failed");
        }
    }
    return make_js_undefined();
}

// process.seteuid(uid)
extern "C" Item js_process_seteuid(Item uid_item) {
    if (get_type_id(uid_item) == LMD_TYPE_INT) {
        int r = seteuid((uid_t)it2i(uid_item));
        if (r != 0) {
            return js_throw_error_with_code("ERR_UNKNOWN_CREDENTIAL",
                "seteuid failed");
        }
    }
    return make_js_undefined();
}

// process.setegid(gid)
extern "C" Item js_process_setegid(Item gid_item) {
    if (get_type_id(gid_item) == LMD_TYPE_INT) {
        int r = setegid((gid_t)it2i(gid_item));
        if (r != 0) {
            return js_throw_error_with_code("ERR_UNKNOWN_CREDENTIAL",
                "setegid failed");
        }
    }
    return make_js_undefined();
}

// process.initgroups(user, group)
extern "C" Item js_process_initgroups(Item user, Item group) {
    (void)user; (void)group;
    return make_js_undefined();
}

// process.setgroups(groups)
extern "C" Item js_process_setgroups(Item groups) {
    (void)groups;
    return make_js_undefined();
}
#endif

// process.emitWarning(warning, type, code) — emit a warning
// Node.js: emits 'warning' event on process; for us, log and call listeners
extern "C" Item js_process_emit(Item event_name, Item arg1);
static Item js_process_emitWarning(Item warning, Item type_item, Item code_item) {
    // Build a Warning object if warning is a string
    Item warning_obj;
    if (get_type_id(warning) == LMD_TYPE_STRING) {
        warning_obj = js_new_object();
        js_property_set(warning_obj, (Item){.item = s2it(heap_create_name("message", 7))}, warning);

        // Check if type_item is an options object { type, code, detail }
        if (get_type_id(type_item) == LMD_TYPE_MAP) {
            Item opt_type = js_property_get(type_item, (Item){.item = s2it(heap_create_name("type", 4))});
            Item opt_code = js_property_get(type_item, (Item){.item = s2it(heap_create_name("code", 4))});
            Item opt_detail = js_property_get(type_item, (Item){.item = s2it(heap_create_name("detail", 6))});
            js_property_set(warning_obj, (Item){.item = s2it(heap_create_name("name", 4))},
                get_type_id(opt_type) == LMD_TYPE_STRING ? opt_type :
                    (Item){.item = s2it(heap_create_name("Warning", 7))});
            if (get_type_id(opt_code) == LMD_TYPE_STRING)
                js_property_set(warning_obj, (Item){.item = s2it(heap_create_name("code", 4))}, opt_code);
            if (get_type_id(opt_detail) == LMD_TYPE_STRING)
                js_property_set(warning_obj, (Item){.item = s2it(heap_create_name("detail", 6))}, opt_detail);
        } else {
            js_property_set(warning_obj, (Item){.item = s2it(heap_create_name("name", 4))},
                get_type_id(type_item) == LMD_TYPE_STRING ? type_item :
                    (Item){.item = s2it(heap_create_name("Warning", 7))});
            if (get_type_id(code_item) == LMD_TYPE_STRING) {
                js_property_set(warning_obj, (Item){.item = s2it(heap_create_name("code", 4))}, code_item);
            }
        }
    } else {
        warning_obj = warning;
    }

    // Emit 'warning' event on process
    js_process_emit((Item){.item = s2it(heap_create_name("warning", 7))}, warning_obj);
    return make_js_undefined();
}

// POSIX: process.getuid/getgid/geteuid/getegid
#ifndef _WIN32
#include <unistd.h>
#include <signal.h>
extern "C" Item js_process_getuid(void) { return (Item){.item = i2it(getuid())}; }
extern "C" Item js_process_getgid(void) { return (Item){.item = i2it(getgid())}; }
extern "C" Item js_process_geteuid(void) { return (Item){.item = i2it(geteuid())}; }
extern "C" Item js_process_getegid(void) { return (Item){.item = i2it(getegid())}; }

// process.kill(pid, signal) — send a signal to a process
extern "C" Item js_process_kill(Item pid_item, Item signal_item) {
    int pid = (int)it2i(pid_item);
    int sig = SIGTERM; // default
    if (get_type_id(signal_item) == LMD_TYPE_INT) {
        sig = (int)it2i(signal_item);
    } else if (get_type_id(signal_item) == LMD_TYPE_STRING) {
        String* s = it2s(signal_item);
        if (s->len == 7 && memcmp(s->chars, "SIGKILL", 7) == 0) sig = SIGKILL;
        else if (s->len == 7 && memcmp(s->chars, "SIGTERM", 7) == 0) sig = SIGTERM;
        else if (s->len == 6 && memcmp(s->chars, "SIGINT", 6) == 0) sig = SIGINT;
        else if (s->len == 7 && memcmp(s->chars, "SIGHUP", 6) == 0) sig = SIGHUP;
        else if (s->len == 7 && memcmp(s->chars, "SIGUSR1", 7) == 0) sig = SIGUSR1;
        else if (s->len == 7 && memcmp(s->chars, "SIGUSR2", 7) == 0) sig = SIGUSR2;
        else if (s->len == 1 && s->chars[0] == '0') sig = 0;
    }
    int r = kill(pid, sig);
    if (r != 0) {
        log_error("process.kill: failed to send signal %d to pid %d", sig, pid);
    }
    return (Item){.item = ITEM_TRUE};
}

// process.getgroups() — returns array of group IDs
extern "C" Item js_process_getgroups(void) {
    gid_t groups[256];
    int ngroups = getgroups(256, groups);
    if (ngroups < 0) ngroups = 0;
    Item arr = js_array_new(ngroups);
    for (int i = 0; i < ngroups; i++) {
        js_array_set(arr, (Item){.item = i2it(i)}, (Item){.item = i2it(groups[i])});
    }
    return arr;
}
#endif

// process.memoryUsage() — returns object with rss, heapTotal, heapUsed, external, arrayBuffers
extern "C" Item js_process_memoryUsage(void) {
    Item result = js_new_object();
    // approximate memory info
#ifdef __APPLE__
    struct task_basic_info info;
    mach_msg_type_number_t size = TASK_BASIC_INFO_COUNT;
    kern_return_t kr = task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &size);
    int64_t rss = (kr == KERN_SUCCESS) ? (int64_t)info.resident_size : 0;
#elif defined(__linux__)
    int64_t rss = 0;
    FILE* f = fopen("/proc/self/statm", "r");
    if (f) {
        long pages = 0;
        if (fscanf(f, "%*ld %ld", &pages) == 1)
            rss = (int64_t)pages * sysconf(_SC_PAGESIZE);
        fclose(f);
    }
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    int64_t rss = 0;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        rss = (int64_t)pmc.WorkingSetSize;
#else
    int64_t rss = 0;
#endif
    js_property_set(result, (Item){.item = s2it(heap_create_name("rss", 3))},
                    (Item){.item = i2it(rss)});
    js_property_set(result, (Item){.item = s2it(heap_create_name("heapTotal", 9))},
                    (Item){.item = i2it(rss)});
    js_property_set(result, (Item){.item = s2it(heap_create_name("heapUsed", 8))},
                    (Item){.item = i2it(rss / 2)});
    js_property_set(result, (Item){.item = s2it(heap_create_name("external", 8))},
                    (Item){.item = i2it(0)});
    js_property_set(result, (Item){.item = s2it(heap_create_name("arrayBuffers", 12))},
                    (Item){.item = i2it(0)});
    return result;
}

// process.cpuUsage() — returns {user, system} in microseconds
extern "C" Item js_process_cpuUsage(void) {
    Item result = js_new_object();
#ifndef _WIN32
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    int64_t user_us = (int64_t)usage.ru_utime.tv_sec * 1000000 + (int64_t)usage.ru_utime.tv_usec;
    int64_t sys_us = (int64_t)usage.ru_stime.tv_sec * 1000000 + (int64_t)usage.ru_stime.tv_usec;
#else
    int64_t user_us = 0, sys_us = 0;
    FILETIME create, exit_t, kernel, user_ft;
    if (GetProcessTimes(GetCurrentProcess(), &create, &exit_t, &kernel, &user_ft)) {
        ULARGE_INTEGER u, k;
        u.LowPart = user_ft.dwLowDateTime; u.HighPart = user_ft.dwHighDateTime;
        k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
        user_us = (int64_t)(u.QuadPart / 10); // 100ns → us
        sys_us = (int64_t)(k.QuadPart / 10);
    }
#endif
    js_property_set(result, (Item){.item = s2it(heap_create_name("user", 4))},
                    (Item){.item = i2it(user_us)});
    js_property_set(result, (Item){.item = s2it(heap_create_name("system", 6))},
                    (Item){.item = i2it(sys_us)});
    return result;
}

// process.umask([mask]) — get/set file mode creation mask
extern "C" Item js_process_umask(Item mask_item) {
#ifndef _WIN32
    TypeId tid = get_type_id(mask_item);
    if (tid == LMD_TYPE_INT) {
        int64_t val = it2i(mask_item);
        mode_t old = umask((mode_t)val);
        return (Item){.item = i2it((int64_t)old)};
    }
    if (tid == LMD_TYPE_STRING) {
        String* s = it2s(mask_item);
        if (s && s->len > 0) {
            // parse as octal string
            char buf[16];
            int len = (int)s->len < 15 ? (int)s->len : 15;
            memcpy(buf, s->chars, (size_t)len);
            buf[len] = '\0';
            char* end = NULL;
            long val = strtol(buf, &end, 8);
            if (end == buf || *end != '\0' || val < 0 || val > 0777) {
                // invalid octal string
                return js_throw_range_error_code("ERR_INVALID_ARG_VALUE", "The argument 'mask' is invalid");
            }
            mode_t old = umask((mode_t)val);
            return (Item){.item = i2it((int64_t)old)};
        }
    }
    if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_ARRAY || tid == LMD_TYPE_BOOL) {
        // invalid type
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", "The \"mask\" argument must be of type number or string");
    }
    // no arg: get current and restore
    mode_t current = umask(0);
    umask(current);
    return (Item){.item = i2it((int64_t)current)};
#else
    return (Item){.item = i2it(0)};
#endif
}

// process.constrainedMemory() — returns 0 (no cgroup constraints on most systems)
extern "C" Item js_process_constrainedMemory(void) {
    return (Item){.item = i2it(0)};
}

// process.availableMemory() — returns an estimate of available memory in bytes
extern "C" Item js_process_availableMemory(void) {
#ifdef __APPLE__
    // rough estimate: free + inactive pages
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vm_stat, &count) == KERN_SUCCESS) {
        int64_t avail = ((int64_t)vm_stat.free_count + (int64_t)vm_stat.inactive_count) * 4096;
        return (Item){.item = i2it(avail)};
    }
#endif
    // fallback: return 0
    return (Item){.item = i2it(0)};
}

// process.abort() — abort the process
extern "C" Item js_process_abort() {
    abort();
    return (Item){.item = ITEM_NULL}; // unreachable
}

// build process.versions object
static Item build_process_versions(void) {
    Item versions = js_new_object();
    js_property_set(versions, (Item){.item = s2it(heap_create_name("node", 4))},
                    (Item){.item = s2it(heap_create_name("20.0.0", 6))});
    js_property_set(versions, (Item){.item = s2it(heap_create_name("lambda", 6))},
                    (Item){.item = s2it(heap_create_name("1.0.0", 5))});
    js_property_set(versions, (Item){.item = s2it(heap_create_name("v8", 2))},
                    (Item){.item = s2it(heap_create_name("0.0.0", 5))});
    js_property_set(versions, (Item){.item = s2it(heap_create_name("uv", 2))},
                    (Item){.item = s2it(heap_create_name("1.0.0", 5))});
    js_property_set(versions, (Item){.item = s2it(heap_create_name("modules", 7))},
                    (Item){.item = s2it(heap_create_name("115", 3))});
    js_property_set(versions, (Item){.item = s2it(heap_create_name("openssl", 7))},
                    (Item){.item = s2it(heap_create_name("3.0.0", 5))});
    js_property_set(versions, (Item){.item = s2it(heap_create_name("zlib", 4))},
                    (Item){.item = s2it(heap_create_name("1.3.0", 5))});
    js_property_set(versions, (Item){.item = s2it(heap_create_name("napi", 4))},
                    (Item){.item = s2it(heap_create_name("9", 1))});
    return versions;
}

static void js_process_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = (Item){.item = s2it(heap_create_name(name, strlen(name)))};
    Item fn = js_new_function(func_ptr, param_count);
    // Mark as non-constructor (native builtin) so .prototype returns undefined
    // Use offset to set builtin_id = -2 (JsFunction layout: type_id, func_ptr, param_count,
    // env, env_size, prototype, bound_this, bound_args, bound_argc, name, builtin_id)
    struct { TypeId t; void* fp; int pc; Item* e; int es; Item p; Item bt; Item* ba; int bc; String* n; int bid; } *fl;
    fl = decltype(fl)(fn.function);
    fl->bid = -2;
    js_property_set(ns, key, fn);
}

// ─── process.on(event, listener) ────────────────────────────────────────────
// simple event emitter for process: supports 'exit', 'uncaughtException', 'beforeExit'
// plus general events via a listener map
#define MAX_PROCESS_LISTENERS 32
static Item process_exit_listeners[MAX_PROCESS_LISTENERS] = {};
static int process_exit_listener_count = 0;
static Item process_uncaught_listeners[MAX_PROCESS_LISTENERS] = {};
static int process_uncaught_listener_count = 0;
static bool js_process_exiting = false;
static Item process_listener_map = {0}; // general event → listener array map
static int process_total_listener_count = 0;

static Item get_process_listener_map() {
    if (process_listener_map.item == 0) {
        process_listener_map = js_new_object();
        heap_register_gc_root(&process_listener_map.item);
    }
    return process_listener_map;
}

extern "C" int64_t js_key_is_symbol_c(Item key);
extern "C" Item js_process_on(Item event_name, Item listener) {
    TypeId etype = get_type_id(event_name);
    bool is_sym = js_key_is_symbol_c(event_name);
    if (etype != LMD_TYPE_STRING && !is_sym) return js_process_object;
    if (get_type_id(listener) != LMD_TYPE_FUNC) return js_process_object;
    if (etype == LMD_TYPE_STRING) {
        String* ev = it2s(event_name);
        if (ev->len == 4 && memcmp(ev->chars, "exit", 4) == 0) {
            if (process_exit_listener_count < MAX_PROCESS_LISTENERS) {
                process_exit_listeners[process_exit_listener_count++] = listener;
            }
        } else if (ev->len == 18 && memcmp(ev->chars, "uncaughtException", 18) == 0) {
            if (process_uncaught_listener_count < MAX_PROCESS_LISTENERS) {
                process_uncaught_listeners[process_uncaught_listener_count++] = listener;
            }
        }
    }

    // also store in general listener map
    Item map = get_process_listener_map();
    Item arr = js_property_get(map, event_name);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) {
        arr = js_array_new(0);
        js_property_set(map, event_name, arr);
    }
    js_array_push(arr, listener);
    process_total_listener_count++;

    // update _eventsCount on process object
    js_property_set(js_process_object,
        (Item){.item = s2it(heap_create_name("_eventsCount", 12))},
        (Item){.item = i2it((int64_t)process_total_listener_count)});

    // return process for chaining
    return js_process_object;
}

// process.emit(event, ...args) — emit an event on process
extern "C" Item js_process_emit(Item event_name, Item arg1) {
    TypeId etype = get_type_id(event_name);
    if (etype != LMD_TYPE_STRING && !js_key_is_symbol_c(event_name)) return (Item){.item = b2it(false)};

    extern Item js_call_function(Item func, Item this_val, Item* args, int nargs);

    Item map = get_process_listener_map();
    Item arr = js_property_get(map, event_name);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return (Item){.item = b2it(false)};
    int64_t len = js_array_length(arr);
    if (len == 0) return (Item){.item = b2it(false)};
    for (int64_t i = 0; i < len; i++) {
        Item listener = js_array_get_int(arr, i);
        if (get_type_id(listener) == LMD_TYPE_FUNC) {
            js_call_function(listener, js_process_object, &arg1, 1);
        }
    }
    return (Item){.item = b2it(true)};
}

extern "C" void js_process_emit_exit(int code) {
    // Guard against double-firing (process.exit() fires then transpiler cleanup fires)
    if (js_process_exiting) return;
    js_process_exiting = true;
    extern Item js_call_function(Item func, Item this_val, Item* args, int nargs);
    Item code_item = (Item){.item = i2it((int64_t)code)};
    for (int i = 0; i < process_exit_listener_count; i++) {
        js_call_function(process_exit_listeners[i], js_process_object, &code_item, 1);
    }
}

extern "C" void js_process_reset_listeners(void) {
    process_exit_listener_count = 0;
    process_uncaught_listener_count = 0;
    js_process_exiting = false;
    process_total_listener_count = 0;
    // don't reset process_listener_map — it'll be GC'd
    process_listener_map = (Item){0};
}

// process.once(event, listener) — like process.on but fires only once
extern "C" Item js_process_once(Item event_name, Item listener) {
    // For simplicity, just register as a normal listener.
    // A full implementation would wrap the listener to auto-remove after first call.
    return js_process_on(event_name, listener);
}

// process.removeListener(event, listener) — no-op for now
extern "C" Item js_process_removeListener(Item event_name, Item listener) {
    return js_process_object;
}

// process.removeAllListeners(event) — no-op for now
extern "C" Item js_process_removeAllListeners(Item event_name) {
    return js_process_object;
}

// process.listenerCount(event) — return count of listeners for event
extern "C" Item js_process_listenerCount(Item event_name) {
    Item map = get_process_listener_map();
    Item arr = js_property_get(map, event_name);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return (Item){.item = i2it(0)};
    return (Item){.item = i2it(js_array_length(arr))};
}

// process.listeners(event) — return array of listeners
extern "C" Item js_process_listeners(Item event_name) {
    Item map = get_process_listener_map();
    Item arr = js_property_get(map, event_name);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return js_array_new(0);
    return arr;
}

// process.hasUncaughtExceptionCaptureCallback()
static Item js_uncaught_exception_cb = {.item = 0};
extern "C" Item js_process_hasUncaughtExceptionCaptureCallback(void) {
    return (Item){.item = b2it(js_uncaught_exception_cb.item != 0 &&
        get_type_id(js_uncaught_exception_cb) == LMD_TYPE_FUNC)};
}

// process.setUncaughtExceptionCaptureCallback(fn)
extern "C" Item js_process_setUncaughtExceptionCaptureCallback(Item fn) {
    TypeId tid = get_type_id(fn);
    if (tid == LMD_TYPE_FUNC) {
        js_uncaught_exception_cb = fn;
    } else if (tid == LMD_TYPE_NULL) {
        js_uncaught_exception_cb = (Item){.item = 0};
    } else {
        extern Item js_throw_type_error_code(const char*, const char*);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"fn\" argument must be of type function or null.");
    }
    return make_js_undefined();
}

// process.getActiveResourcesInfo() — stub returns empty array
extern "C" Item js_process_getActiveResourcesInfo(void) {
    return js_array_new(0);
}

// process.setSourceMapsEnabled(val) — stub no-op
extern "C" Item js_process_setSourceMapsEnabled(Item val) {
    (void)val;
    return make_js_undefined();
}

extern "C" Item js_get_process_object_value(void) {
    if (js_process_object.item == ITEM_NULL) {
        js_process_object = js_object_create(ItemNull);
        heap_register_gc_root(&js_process_object.item);

        // argv
        Item argv_key = (Item){.item = s2it(heap_create_name("argv", 4))};
        js_property_set(js_process_object, argv_key, js_get_process_argv());

        // pid, ppid
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("pid", 3))},
            (Item){.item = i2it((int64_t)getpid())});
#ifndef _WIN32
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("ppid", 4))},
            (Item){.item = i2it((int64_t)getppid())});
#endif

        // platform, arch, version
#ifdef __APPLE__
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("platform", 8))},
            (Item){.item = s2it(heap_create_name("darwin", 6))});
#elif defined(__linux__)
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("platform", 8))},
            (Item){.item = s2it(heap_create_name("linux", 5))});
#elif defined(_WIN32)
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("platform", 8))},
            (Item){.item = s2it(heap_create_name("win32", 5))});
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("arch", 4))},
            (Item){.item = s2it(heap_create_name("arm64", 5))});
#elif defined(__x86_64__) || defined(_M_X64)
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("arch", 4))},
            (Item){.item = s2it(heap_create_name("x64", 3))});
#endif

        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("version", 7))},
            (Item){.item = s2it(heap_create_name("v20.0.0", 7))});

        // methods: cwd, chdir, exit, uptime, hrtime.bigint
        js_process_set_method(js_process_object, "cwd", (void*)js_process_cwd, 0);
        js_process_set_method(js_process_object, "chdir", (void*)js_process_chdir, 1);
        js_process_set_method(js_process_object, "exit", (void*)js_process_exit, 1);
        js_process_set_method(js_process_object, "uptime", (void*)js_process_uptime, 0);
        js_process_set_method(js_process_object, "nextTick", (void*)js_process_nextTick, -1);
        js_process_set_method(js_process_object, "memoryUsage", (void*)js_process_memoryUsage, 0);
        js_process_set_method(js_process_object, "cpuUsage", (void*)js_process_cpuUsage, 0);
        js_process_set_method(js_process_object, "umask", (void*)js_process_umask, 1);
        js_process_set_method(js_process_object, "abort", (void*)js_process_abort, 0);
        js_process_set_method(js_process_object, "constrainedMemory", (void*)js_process_constrainedMemory, 0);
        js_process_set_method(js_process_object, "availableMemory", (void*)js_process_availableMemory, 0);
        js_process_set_method(js_process_object, "hasUncaughtExceptionCaptureCallback", (void*)js_process_hasUncaughtExceptionCaptureCallback, 0);
        js_process_set_method(js_process_object, "setUncaughtExceptionCaptureCallback", (void*)js_process_setUncaughtExceptionCaptureCallback, 1);
        js_process_set_method(js_process_object, "getActiveResourcesInfo", (void*)js_process_getActiveResourcesInfo, 0);
        js_process_set_method(js_process_object, "setSourceMapsEnabled", (void*)js_process_setSourceMapsEnabled, 1);
        js_process_set_method(js_process_object, "on", (void*)js_process_on, 2);
        js_process_set_method(js_process_object, "addListener", (void*)js_process_on, 2);
        js_process_set_method(js_process_object, "once", (void*)js_process_once, 2);
        js_process_set_method(js_process_object, "emit", (void*)js_process_emit, 2);
        js_process_set_method(js_process_object, "off", (void*)js_process_removeListener, 2);
        js_process_set_method(js_process_object, "removeListener", (void*)js_process_removeListener, 2);
        js_process_set_method(js_process_object, "removeAllListeners", (void*)js_process_removeAllListeners, 1);
        js_process_set_method(js_process_object, "listenerCount", (void*)js_process_listenerCount, 1);
        js_process_set_method(js_process_object, "listeners", (void*)js_process_listeners, 1);
        js_process_set_method(js_process_object, "prependListener", (void*)js_process_on, 2);
        js_process_set_method(js_process_object, "prependOnceListener", (void*)js_process_once, 2);

        // versions object
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("versions", 8))}, build_process_versions());

        // title
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("title", 5))},
            (Item){.item = s2it(heap_create_name("lambda", 6))});

        // hrtime function with bigint() method
        Item hrtime_fn = js_new_function((void*)js_process_hrtime, 1);
        js_process_set_method(hrtime_fn, "bigint", (void*)js_process_hrtime_bigint, 0);
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("hrtime", 6))}, hrtime_fn);

        // env
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("env", 3))}, build_process_env());

        // stdout, stderr, stdin
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("stdout", 6))}, build_process_stdout());
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("stderr", 6))}, build_process_stderr());
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("stdin", 5))}, build_process_stdin());

        // exitCode — default 0
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("exitCode", 8))},
            (Item){.item = i2it(0)});

        // execPath — absolute path to the lambda.exe binary
        if (js_process_argv_raw && js_process_argc_raw > 0) {
            char execpath_buf[1024];
            if (realpath(js_process_argv_raw[0], execpath_buf)) {
                js_property_set(js_process_object,
                    (Item){.item = s2it(heap_create_name("execPath", 8))},
                    (Item){.item = s2it(heap_create_name(execpath_buf, (int)strlen(execpath_buf)))});
            } else {
                js_property_set(js_process_object,
                    (Item){.item = s2it(heap_create_name("execPath", 8))},
                    (Item){.item = s2it(heap_create_name(js_process_argv_raw[0]))});
            }
        }

        // execArgv — empty array (no V8 flags)
        {
            Item execArgv_arr = js_array_new(0);
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("execArgv", 8))},
                execArgv_arr);
        }

        // config — minimal process.config for Node.js compat
        {
            Item config_obj = js_new_object();
            Item variables_obj = js_new_object();
            js_property_set(variables_obj,
                (Item){.item = s2it(heap_create_name("v8_enable_i18n_support", 22))},
                (Item){.item = i2it(0)});
            js_property_set(variables_obj,
                (Item){.item = s2it(heap_create_name("node_shared", 11))},
                (Item){.item = ITEM_FALSE});
            js_property_set(variables_obj,
                (Item){.item = s2it(heap_create_name("node_use_ffi", 12))},
                (Item){.item = ITEM_FALSE});
            js_property_set(config_obj,
                (Item){.item = s2it(heap_create_name("variables", 9))},
                variables_obj);
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("config", 6))},
                config_obj);
        }

        // features — minimal process.features for Node.js compat
        {
            Item features_obj = js_new_object();
            js_property_set(features_obj,
                (Item){.item = s2it(heap_create_name("inspector", 9))},
                (Item){.item = ITEM_FALSE});
            js_property_set(features_obj,
                (Item){.item = s2it(heap_create_name("debug", 5))},
                (Item){.item = ITEM_FALSE});
            js_property_set(features_obj,
                (Item){.item = s2it(heap_create_name("uv", 2))},
                (Item){.item = ITEM_TRUE});
            js_property_set(features_obj,
                (Item){.item = s2it(heap_create_name("tls_alpn", 8))},
                (Item){.item = ITEM_FALSE});
            js_property_set(features_obj,
                (Item){.item = s2it(heap_create_name("tls_sni", 7))},
                (Item){.item = ITEM_FALSE});
            js_property_set(features_obj,
                (Item){.item = s2it(heap_create_name("tls_ocsp", 8))},
                (Item){.item = ITEM_FALSE});
            js_property_set(features_obj,
                (Item){.item = s2it(heap_create_name("tls", 3))},
                (Item){.item = ITEM_FALSE});
            js_property_set(features_obj,
                (Item){.item = s2it(heap_create_name("ipv6", 4))},
                (Item){.item = ITEM_TRUE});
            js_property_set(features_obj,
                (Item){.item = s2it(heap_create_name("openssl_is_boringssl", 20))},
                (Item){.item = ITEM_FALSE});
            js_property_set(features_obj,
                (Item){.item = s2it(heap_create_name("quic", 4))},
                (Item){.item = ITEM_FALSE});
            js_property_set(features_obj,
                (Item){.item = s2it(heap_create_name("cached_builtins", 15))},
                (Item){.item = ITEM_TRUE});
            js_property_set(features_obj,
                (Item){.item = s2it(heap_create_name("require_module", 14))},
                (Item){.item = ITEM_TRUE});
            js_property_set(features_obj,
                (Item){.item = s2it(heap_create_name("typescript", 10))},
                (Item){.item = ITEM_FALSE});
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("features", 8))},
                features_obj);
        }

        // POSIX: process.getuid(), getgid(), geteuid(), getegid()
#ifndef _WIN32
        {
            extern Item js_process_getuid(void);
            extern Item js_process_getgid(void);
            extern Item js_process_geteuid(void);
            extern Item js_process_getegid(void);
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("getuid", 6))},
                js_new_function((void*)js_process_getuid, 0));
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("getgid", 6))},
                js_new_function((void*)js_process_getgid, 0));
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("geteuid", 7))},
                js_new_function((void*)js_process_geteuid, 0));
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("getegid", 7))},
                js_new_function((void*)js_process_getegid, 0));
            extern Item js_process_kill(Item, Item);
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("kill", 4))},
                js_new_function((void*)js_process_kill, 2));
            extern Item js_process_getgroups(void);
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("getgroups", 9))},
                js_new_function((void*)js_process_getgroups, 0));
        }
#endif

        // process.argv0 — the original argv[0] value
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("argv0", 5))},
            (Item){.item = s2it(heap_create_name("lambda", 6))});

        // process.emitWarning(warning, type, code)
        js_property_set(js_process_object,
            (Item){.item = s2it(heap_create_name("emitWarning", 11))},
            js_new_function((void*)js_process_emitWarning, 3));

        // process.release — Node.js compat
        {
            Item release_obj = js_new_object();
            js_property_set(release_obj,
                (Item){.item = s2it(heap_create_name("name", 4))},
                (Item){.item = s2it(heap_create_name("node", 4))});
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("release", 7))},
                release_obj);
        }

        // process.binding(name) — deprecated, but tests check it exists
        {
            extern Item js_process_binding(Item name);
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("binding", 7))},
                js_new_function((void*)js_process_binding, 1));
            extern Item js_process_dlopen(Item module, Item filename);
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("dlopen", 6))},
                js_new_function((void*)js_process_dlopen, 2));
        }

        // process.allowedNodeEnvironmentFlags — Set of known flags
        {
            // Create an empty Set-like object with .has() method
            Item flags = js_new_object();
            extern Item js_set_has_stub(Item self, Item key);
            js_property_set(flags,
                (Item){.item = s2it(heap_create_name("has", 3))},
                js_new_function((void*)js_set_has_stub, 1));
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("allowedNodeEnvironmentFlags", 27))},
                flags);
        }

        // process.report — diagnostic report stub
        {
            Item report = js_new_object();
            extern Item js_process_report_getReport(void);
            js_property_set(report,
                (Item){.item = s2it(heap_create_name("getReport", 9))},
                js_new_function((void*)js_process_report_getReport, 0));
            js_property_set(report,
                (Item){.item = s2it(heap_create_name("directory", 9))},
                (Item){.item = s2it(heap_create_name("", 0))});
            js_property_set(report,
                (Item){.item = s2it(heap_create_name("filename", 8))},
                (Item){.item = s2it(heap_create_name("", 0))});
            js_property_set(report,
                (Item){.item = s2it(heap_create_name("compact", 7))},
                (Item){.item = ITEM_FALSE});
            js_property_set(report,
                (Item){.item = s2it(heap_create_name("reportOnFatalError", 18))},
                (Item){.item = ITEM_FALSE});
            js_property_set(report,
                (Item){.item = s2it(heap_create_name("reportOnSignal", 14))},
                (Item){.item = ITEM_FALSE});
            js_property_set(report,
                (Item){.item = s2it(heap_create_name("reportOnUncaughtException", 25))},
                (Item){.item = ITEM_FALSE});
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("report", 6))},
                report);
        }

#ifndef _WIN32
        // process.setuid / process.setgid
        {
            extern Item js_process_setuid(Item uid);
            extern Item js_process_setgid(Item gid);
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("setuid", 6))},
                js_new_function((void*)js_process_setuid, 1));
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("setgid", 6))},
                js_new_function((void*)js_process_setgid, 1));
            extern Item js_process_seteuid(Item uid);
            extern Item js_process_setegid(Item gid);
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("seteuid", 7))},
                js_new_function((void*)js_process_seteuid, 1));
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("setegid", 7))},
                js_new_function((void*)js_process_setegid, 1));
            extern Item js_process_initgroups(Item user, Item group);
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("initgroups", 10))},
                js_new_function((void*)js_process_initgroups, 2));
            extern Item js_process_setgroups(Item groups);
            js_property_set(js_process_object,
                (Item){.item = s2it(heap_create_name("setgroups", 9))},
                js_new_function((void*)js_process_setgroups, 1));
        }
#endif
    }
    return js_process_object;
}

// =============================================================================
// setImmediate / clearImmediate
// =============================================================================

// setImmediate(callback) — schedule callback as a microtask (next tick)
extern "C" Item js_setImmediate(Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        extern Item js_throw_type_error_code(const char*, const char*);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"callback\" argument must be of type function.");
    }
    extern Item js_setTimeout(Item cb, Item delay);
    return js_setTimeout(callback, (Item){.item = i2it(0)});
}

// setImmediate with extra args passed as a JS array (used by transpiler)
extern "C" Item js_setImmediate_with_args(Item callback, Item args_array) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        extern Item js_throw_type_error_code(const char*, const char*);
        return js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"callback\" argument must be of type function.");
    }
    extern Item js_setTimeout_args(Item cb, Item delay, Item args_array);
    return js_setTimeout_args(callback, (Item){.item = i2it(0)}, args_array);
}

// clearImmediate(id) — cancel a setImmediate
extern "C" void js_clearImmediate(Item id) {
    extern void js_clearTimeout(Item id);
    js_clearTimeout(id);
}

// =============================================================================
// structuredClone(value) — deep clone
// =============================================================================

static Item structured_clone_impl(Item value, int depth) {
    if (depth > 100) return value; // prevent infinite recursion
    TypeId tid = get_type_id(value);

    // primitives: return as-is
    if (tid == LMD_TYPE_NULL || tid == LMD_TYPE_UNDEFINED ||
        tid == LMD_TYPE_BOOL || tid == LMD_TYPE_INT ||
        tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_STRING) {
        return value;
    }

    // arrays: deep clone each element
    if (tid == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(value);
        Item result = js_array_new((int)len);
        for (int64_t i = 0; i < len; i++) {
            Item elem = js_array_get_int(value, i);
            js_array_push(result, structured_clone_impl(elem, depth + 1));
        }
        return result;
    }

    // typed array: copy buffer
    extern bool js_is_typed_array(Item item);
    extern JsTypedArray* js_get_typed_array_ptr(Map* m);
    if (js_is_typed_array(value)) {
        Map* m = value.map;
        JsTypedArray* ta = js_get_typed_array_ptr(m);
        if (ta && ta->data && ta->byte_length > 0) {
            extern Item js_typed_array_new(int element_type, int length);
            Item clone = js_typed_array_new(ta->element_type, ta->byte_length);
            Map* cm = clone.map;
            JsTypedArray* cta = js_get_typed_array_ptr(cm);
            if (cta && cta->data) {
                memcpy(cta->data, ta->data, ta->byte_length);
            }
            return clone;
        }
        return value;
    }

    // maps/objects: clone properties
    if (tid == LMD_TYPE_MAP) {
        Item result = js_new_object();
        Item keys = js_object_keys(value);
        int64_t len = js_array_length(keys);
        for (int64_t i = 0; i < len; i++) {
            Item key = js_array_get_int(keys, i);
            Item val = js_property_get(value, key);
            js_property_set(result, key, structured_clone_impl(val, depth + 1));
        }
        return result;
    }

    // functions can't be cloned
    if (tid == LMD_TYPE_FUNC) {
        return value;
    }

    return value;
}

extern "C" Item js_structuredClone(Item value) {
    return structured_clone_impl(value, 0);
}

// =============================================================================
// Global Functions
// =============================================================================

extern "C" Item js_parseInt(Item str_item, Item radix_item) {
    // Convert first arg to string
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    // Get radix per ES spec: ToInt32(radix), then validate 2-36
    int radix = 10;
    bool radix_explicit = false;
    TypeId rtype = get_type_id(radix_item);
    if (rtype != LMD_TYPE_UNDEFINED) {
        // Convert radix to number (handles strings, booleans, boxed numbers, etc.)
        Item radix_num = js_to_number(radix_item);
        TypeId rn_type = get_type_id(radix_num);
        double rdbl = 0;
        if (rn_type == LMD_TYPE_INT) rdbl = (double)it2i(radix_num);
        else if (rn_type == LMD_TYPE_FLOAT) rdbl = it2d(radix_num);
        // ToInt32: NaN, Infinity, -Infinity → 0
        if (isnan(rdbl) || isinf(rdbl)) {
            radix = 0;
        } else {
            // ToInt32 per ES spec: modulo 2^32, then signed 32-bit
            double d = fmod(trunc(rdbl), 4294967296.0);
            if (d < 0) d += 4294967296.0;
            if (d >= 2147483648.0) d -= 4294967296.0;
            radix = (int)d;
        }
        radix_explicit = true;
    }
    if (radix == 0) { radix = 10; radix_explicit = false; }
    // Invalid radix: return NaN
    if (radix_explicit && (radix < 2 || radix > 36)) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    // Null-terminate
    char buf[256];
    int len = s->len < 255 ? s->len : 255;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';

    // Skip whitespace (ES spec StrWhiteSpaceChar: ASCII + Unicode whitespace)
    char* start = buf;
    char* end_buf = buf + len;
    for (;;) {
        if (start >= end_buf) break;
        unsigned char c = (unsigned char)*start;
        // ASCII whitespace: space, tab, LF, CR, FF, VT
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') { start++; continue; }
        // 2-byte UTF-8: U+00A0 (NBSP) = 0xC2 0xA0
        if (c == 0xC2 && start + 1 < end_buf && (unsigned char)start[1] == 0xA0) { start += 2; continue; }
        // 3-byte UTF-8 whitespace chars
        if (c == 0xE2 && start + 2 < end_buf) {
            unsigned char b1 = (unsigned char)start[1], b2 = (unsigned char)start[2];
            // U+2000-U+200A (en/em space etc) = E2 80 80..8A
            if (b1 == 0x80 && b2 >= 0x80 && b2 <= 0x8A) { start += 3; continue; }
            // U+2028 (LS) = E2 80 A8, U+2029 (PS) = E2 80 A9
            if (b1 == 0x80 && (b2 == 0xA8 || b2 == 0xA9)) { start += 3; continue; }
            // U+202F (narrow NBSP) = E2 80 AF
            if (b1 == 0x80 && b2 == 0xAF) { start += 3; continue; }
            // U+205F (medium math space) = E2 81 9F
            if (b1 == 0x81 && b2 == 0x9F) { start += 3; continue; }
        }
        // U+3000 (ideographic space) = E3 80 80
        if (c == 0xE3 && start + 2 < end_buf && (unsigned char)start[1] == 0x80 && (unsigned char)start[2] == 0x80) { start += 3; continue; }
        // U+FEFF (BOM/ZWNBSP) = EF BB BF
        if (c == 0xEF && start + 2 < end_buf && (unsigned char)start[1] == 0xBB && (unsigned char)start[2] == 0xBF) { start += 3; continue; }
        // U+1680 (ogham space) = E1 9A 80
        if (c == 0xE1 && start + 2 < end_buf && (unsigned char)start[1] == 0x9A && (unsigned char)start[2] == 0x80) { start += 3; continue; }
        break;
    }

    // Parse sign (before hex prefix per ES spec)
    int sign = 1;
    if (*start == '-') { sign = -1; start++; }
    else if (*start == '+') { start++; }

    // Auto-detect hex prefix (0x/0X) when no explicit radix
    if (!radix_explicit && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        radix = 16;
        start += 2;
    } else if (radix == 16 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        start += 2; // skip 0x prefix even with explicit radix 16
    }

    // Manual parseInt per ES spec: accumulate as double to handle large values
    double result = 0;
    bool found_digit = false;
    while (*start) {
        int digit = -1;
        char ch = *start;
        if (ch >= '0' && ch <= '9') digit = ch - '0';
        else if (ch >= 'a' && ch <= 'z') digit = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'Z') digit = ch - 'A' + 10;
        if (digit < 0 || digit >= radix) break;
        found_digit = true;
        result = result * radix + digit;
        start++;
    }
    if (!found_digit) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }
    result *= sign;

    // Return as int if it fits, otherwise as double
    if (result >= -9007199254740992.0 && result <= 9007199254740992.0 &&
        result == (double)(int64_t)result) {
        return (Item){.item = i2it((int64_t)result)};
    }
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = result;
    return (Item){.item = d2it(fp)};
}

extern "C" Item js_parseFloat(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    char buf[256];
    int len = s->len < 255 ? s->len : 255;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';

    // ES spec: skip leading StrWhiteSpaceChar (includes Unicode whitespace)
    char* p = buf;
    while (*p) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') { p++; continue; }
        // UTF-8 two-byte: U+00A0 (NBSP) = C2 A0
        if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xA0) { p += 2; continue; }
        // UTF-8 three-byte Unicode whitespace:
        // U+FEFF (BOM) = EF BB BF
        if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) { p += 3; continue; }
        // U+2000-U+200A (various spaces), U+2028 (LS), U+2029 (PS), U+202F, U+205F, U+3000
        // All encoded as E2 8x xx or E3 80 80
        if ((unsigned char)p[0] == 0xE2) {
            unsigned char b1 = (unsigned char)p[1], b2 = (unsigned char)p[2];
            // U+2000-U+200A: E2 80 80 - E2 80 8A
            if (b1 == 0x80 && b2 >= 0x80 && b2 <= 0x8A) { p += 3; continue; }
            // U+2028: E2 80 A8, U+2029: E2 80 A9
            if (b1 == 0x80 && (b2 == 0xA8 || b2 == 0xA9)) { p += 3; continue; }
            // U+202F: E2 80 AF
            if (b1 == 0x80 && b2 == 0xAF) { p += 3; continue; }
            // U+205F: E2 81 9F
            if (b1 == 0x81 && b2 == 0x9F) { p += 3; continue; }
        }
        // U+1680 (Ogham space mark): E1 9A 80
        if ((unsigned char)p[0] == 0xE1 && (unsigned char)p[1] == 0x9A && (unsigned char)p[2] == 0x80) { p += 3; continue; }
        // U+3000 (ideographic space): E3 80 80
        if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0x80) { p += 3; continue; }
        break;
    }

    // ES spec: parseFloat only parses StrDecimalLiteral — no hex (0x), no 0o, no 0b.
    // strtod parses hex on many platforms, so we must guard against it.
    // StrDecimalLiteral: [+-]? (Infinity | DecimalDigits [. DecimalDigits] [eE [+-] DecimalDigits] | . DecimalDigits [eE ...])
    char* start = p;
    if (*p == '+' || *p == '-') p++;
    if (*p == 'I') {
        // check for "Infinity"
        if (strncmp(p, "Infinity", 8) == 0) {
            double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *fp = (*start == '-') ? -HUGE_VAL : HUGE_VAL;
            return (Item){.item = d2it(fp)};
        }
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }
    // must start with a decimal digit or '.'
    if ((*p < '0' || *p > '9') && *p != '.') {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }
    // '.' alone without a following digit is not valid
    if (*p == '.' && (p[1] < '0' || p[1] > '9')) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    // scan to find the end of the valid decimal literal (no hex)
    char* q = p;
    while (*q >= '0' && *q <= '9') q++;         // integer part
    if (*q == '.') { q++; while (*q >= '0' && *q <= '9') q++; } // fractional part
    if (*q == 'e' || *q == 'E') {                // exponent
        q++;
        if (*q == '+' || *q == '-') q++;
        if (*q >= '0' && *q <= '9') { while (*q >= '0' && *q <= '9') q++; }
        else { q = q - (q[-1] == '+' || q[-1] == '-' ? 2 : 1); } // no digits after e → backtrack
    }

    // null-terminate at the end of valid decimal literal and parse
    char saved = *q;
    *q = '\0';
    char* endptr;
    double val = strtod(start, &endptr);
    *q = saved;

    if (endptr == start) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = val;
    return (Item){.item = d2it(fp)};
}

extern "C" Item js_isNaN(Item value) {
    // ES spec: ToNumber(Symbol) throws TypeError
    if (get_type_id(value) == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE) {
        js_throw_type_error("Cannot convert a Symbol value to a number");
        return ItemNull;
    }
    Item num = js_to_number(value);
    if (js_check_exception()) return ItemNull;
    TypeId type = get_type_id(num);
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(num);
        return (Item){.item = isnan(d) ? ITEM_TRUE : ITEM_FALSE};
    }
    return (Item){.item = ITEM_FALSE};
}

extern "C" Item js_isFinite(Item value) {
    // ES spec: ToNumber(Symbol) throws TypeError
    if (get_type_id(value) == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE) {
        js_throw_type_error("Cannot convert a Symbol value to a number");
        return ItemNull;
    }
    Item num = js_to_number(value);
    if (js_check_exception()) return ItemNull;
    TypeId type = get_type_id(num);
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(num);
        return (Item){.item = isfinite(d) ? ITEM_TRUE : ITEM_FALSE};
    }
    if (type == LMD_TYPE_INT) {
        return (Item){.item = ITEM_TRUE};
    }
    return (Item){.item = ITEM_FALSE};
}

// =============================================================================
// Number Methods
// =============================================================================

// ES-spec rounding helper: round to 'keep' significant digits using round-half-up.
// Uses string-based rounding on snprintf output to avoid intermediate FP precision loss.
// out_digits: receives the rounded significant digit characters
// out_len: receives the count of significant digits
// out_exp: receives the (possibly adjusted) base-10 exponent
static void js_round_sig_digits(double abs_d, int keep,
                                char* out_digits, int* out_len, int* out_exp) {
    // Format with full double precision (20 decimal places = 21 sig digits)
    char wide[64];
    snprintf(wide, sizeof(wide), "%.20e", abs_d);

    // Parse: digit.digits e [+/-] exp
    char all_digits[32];
    int all_count = 0;
    char* p = wide;
    all_digits[all_count++] = *p++; // first significant digit
    if (*p == '.') p++;
    while (*p && *p != 'e' && all_count < 30) {
        all_digits[all_count++] = *p++;
    }
    int exp_val = 0;
    if (*p == 'e') exp_val = atoi(p + 1);

    // Copy first 'keep' digits (pad with '0' if needed)
    for (int i = 0; i < keep; i++) {
        out_digits[i] = (i < all_count) ? all_digits[i] : '0';
    }
    *out_len = keep;
    *out_exp = exp_val;

    // Check rounding digit at position 'keep'
    if (keep < all_count) {
        int rd = all_digits[keep] - '0';
        bool round_up = false;
        if (rd > 5) {
            round_up = true;
        } else if (rd == 5) {
            // ES spec: ties round up (pick larger n)
            // But first check if remaining digits make it > 0.5
            round_up = true; // assume tie → round up
            for (int i = keep + 1; i < all_count; i++) {
                if (all_digits[i] != '0') {
                    round_up = true; // definitely > 0.5
                    break;
                }
            }
        }
        if (round_up) {
            for (int i = keep - 1; i >= 0; i--) {
                out_digits[i]++;
                if (out_digits[i] <= '9') break;
                out_digits[i] = '0';
                if (i == 0) {
                    // Carry out: result becomes 10...0, adjust exponent
                    out_digits[0] = '1';
                    for (int j = 1; j < keep; j++) out_digits[j] = '0';
                    (*out_exp)++;
                }
            }
        }
    }
}

extern "C" Item js_toFixed(Item num_item, Item digits_item) {
    double num;
    TypeId type = get_type_id(num_item);
    if (type == LMD_TYPE_FLOAT) {
        num = it2d(num_item);
    } else if (type == LMD_TYPE_INT) {
        num = (double)it2i(num_item);
    } else {
        return js_to_string(num_item);
    }

    // Step 1-4 per spec: validate fractionDigits BEFORE checking NaN
    // ES spec: ToInteger(fractionDigits) — call ToNumber first, then truncate
    int digits = 0;
    TypeId dtype = get_type_id(digits_item);
    if (dtype == LMD_TYPE_UNDEFINED) {
        digits = 0;
    } else {
        // ES §7.1.4 ToNumber: Symbol → TypeError. Some js_to_number paths
        // currently return NaN silently for Symbols, which would let toFixed
        // fall through to digits=0 — spec mandates TypeError before any
        // RangeError checks.
        if (js_key_is_symbol_c(digits_item)) {
            return js_throw_type_error("Cannot convert a Symbol value to a number");
        }
        Item coerced = js_to_number(digits_item);
        if (js_check_exception()) return ItemNull;
        TypeId ct = get_type_id(coerced);
        double fd = 0;
        if (ct == LMD_TYPE_INT) fd = (double)it2i(coerced);
        else if (ct == LMD_TYPE_FLOAT) fd = it2d(coerced);
        if (isnan(fd)) fd = 0;
        digits = (int)fd;
    }

    // ES spec: RangeError if digits < 0 or > 100
    if (digits < 0 || digits > 100) {
        return js_throw_range_error("toFixed() digits argument must be between 0 and 100");
    }

    // Step 5: If x is NaN, return "NaN"
    if (isnan(num)) return (Item){.item = s2it(heap_create_name("NaN", 3))};

    // Step 6: If x is not finite, format Infinity
    if (isinf(num)) return num > 0
        ? (Item){.item = s2it(heap_create_name("Infinity", 8))}
        : (Item){.item = s2it(heap_create_name("-Infinity", 9))};

    // ES spec step 9: If x >= 10^21, return ToString(x)
    if (fabs(num) >= 1e21) {
        return js_to_string(num_item);
    }

    char buf[128];
    bool negative = num < 0;
    double abs_num = fabs(num);

    // Use string-based rounding: format with full precision, then round manually
    // to avoid intermediate floating-point precision loss
    if (abs_num == 0.0) {
        char* p = buf;
        if (negative) *p++ = '-';
        *p++ = '0';
        if (digits > 0) {
            *p++ = '.';
            for (int i = 0; i < digits; i++) *p++ = '0';
        }
        *p = '\0';
    } else {
        // Get significant digits and exponent via helper
        char sig[128];
        int sig_len, exp_val;
        // Number of significant digits needed: exponent + 1 + digits
        int exp_est = (int)floor(log10(abs_num));
        int keep = exp_est + 1 + digits;
        if (keep <= 0) {
            // Value is too small for any digit at this precision
            // Check if we should round up to 10^(-digits)
            char wide[64];
            snprintf(wide, sizeof(wide), "%.20e", abs_num);
            // Parse exponent from wide
            char* ep = strchr(wide, 'e');
            int we = ep ? atoi(ep + 1) : 0;
            // The value is abs_num = sig * 10^we, we need to check if abs_num * 10^digits >= 0.5
            // i.e., if the (digits+1)th decimal place rounds up
            // Simplified: if keep == 0, check the first sig digit for >= 5
            // if keep < 0, result is always 0
            bool round_up = false;
            if (keep == 0) {
                // First sig digit determines rounding
                int first_digit = wide[0] - '0';
                if (first_digit > 5) round_up = true;
                else if (first_digit == 5) {
                    // Check remaining digits
                    round_up = true;
                    char* dp = wide + 2; // skip first digit and '.'
                    while (*dp && *dp != 'e') {
                        if (*dp != '0') { round_up = true; break; }
                        dp++;
                    }
                }
            }
            char* p = buf;
            if (round_up) {
                if (negative) *p++ = '-';
                // Result: 0.00...01 with digits decimal places
                *p++ = '0';
                if (digits > 0) {
                    *p++ = '.';
                    for (int i = 0; i < digits - 1; i++) *p++ = '0';
                    *p++ = '1';
                } else {
                    *p++ = '1'; // shouldn't happen since keep=0 implies exp+1+d=0
                }
            } else {
                *p++ = '0';
                if (digits > 0) {
                    *p++ = '.';
                    for (int i = 0; i < digits; i++) *p++ = '0';
                }
            }
            *p = '\0';
        } else {
            if (keep > 21) keep = 21; // clamp to double precision
            js_round_sig_digits(abs_num, keep, sig, &sig_len, &exp_val);
            // Build fixed-point string from significant digits and exponent
            // The number is: sig_digits * 10^(exp_val - sig_len + 1)
            // Integer part has exp_val + 1 digits, fractional part has digits digits
            char* p = buf;
            if (negative) *p++ = '-';
            int int_part_len = exp_val + 1; // number of digits before decimal
            if (int_part_len <= 0) {
                *p++ = '0';
                if (digits > 0) {
                    *p++ = '.';
                    for (int i = 0; i < -int_part_len && i < digits; i++) *p++ = '0';
                    int remaining = digits - (-int_part_len);
                    for (int i = 0; i < remaining && i < sig_len; i++) *p++ = sig[i];
                    int written = (-int_part_len) + (remaining < sig_len ? remaining : sig_len);
                    for (int i = written; i < digits; i++) *p++ = '0';
                }
            } else {
                for (int i = 0; i < int_part_len; i++) {
                    *p++ = (i < sig_len) ? sig[i] : '0';
                }
                if (digits > 0) {
                    *p++ = '.';
                    for (int i = 0; i < digits; i++) {
                        int si = int_part_len + i;
                        *p++ = (si < sig_len) ? sig[si] : '0';
                    }
                }
            }
            *p = '\0';
        }
    }
    return (Item){.item = s2it(heap_create_name(buf))};
}

extern "C" Item js_number_method(Item num, Item method_name, Item* args, int argc) {
    // Symbols are encoded as negative ints — route to generic property dispatch
    if (js_is_symbol_item(num)) {
        Item fn = js_property_get(num, method_name);
        if (get_type_id(fn) == LMD_TYPE_FUNC)
            return js_call_function(fn, num, args, argc);
        return ItemNull;
    }
    if (get_type_id(method_name) != LMD_TYPE_STRING) return ItemNull;
    String* method = it2s(method_name);
    if (!method) return ItemNull;

    // BigInt prototype methods
    if (get_type_id(num) == LMD_TYPE_DECIMAL) {
        Decimal* dec = (Decimal*)(num.item & 0x00FFFFFFFFFFFFFF);
        if (dec && dec->unlimited == DECIMAL_BIGINT) {
            if (method->len == 8 && strncmp(method->chars, "toString", 8) == 0) {
                int radix = 10;
                if (argc > 0 && get_type_id(args[0]) != LMD_TYPE_UNDEFINED) {
                    Item radix_item = js_to_number(args[0]);
                    if (js_check_exception()) return ItemNull;
                    TypeId rt = get_type_id(radix_item);
                    if (rt == LMD_TYPE_INT) radix = (int)it2i(radix_item);
                    else if (rt == LMD_TYPE_FLOAT) radix = (int)it2d(radix_item);
                    if (radix < 2 || radix > 36) {
                        return js_throw_range_error("toString() radix must be between 2 and 36");
                    }
                }
                char* s = bigint_to_cstring_radix(num, radix);
                if (!s) return ItemNull;
                Item result = (Item){.item = s2it(heap_create_name(s))};
                free(s);
                return result;
            }
            if (method->len == 7 && strncmp(method->chars, "valueOf", 7) == 0) {
                return num;
            }
            if (method->len == 14 && strncmp(method->chars, "toLocaleString", 14) == 0) {
                return js_to_string(num);
            }
            // BigInt doesn't have toFixed, toPrecision, toExponential
            js_throw_type_error("is not a function");
            return ItemNull;
        }
    }

    if (method->len == 7 && strncmp(method->chars, "toFixed", 7) == 0) {
        Item digits = (argc > 0) ? args[0] : (Item){.item = i2it(0)};
        return js_toFixed(num, digits);
    }
    if (method->len == 8 && strncmp(method->chars, "toString", 8) == 0) {
        // per spec: ToInteger(radix) + range check BEFORE NaN/Infinity handling
        if (argc > 0 && get_type_id(args[0]) != LMD_TYPE_UNDEFINED) {
            Item radix_item = js_to_number(args[0]);
            if (js_check_exception()) return ItemNull;
            int radix = 10;
            TypeId rt = get_type_id(radix_item);
            if (rt == LMD_TYPE_INT) radix = (int)it2i(radix_item);
            else if (rt == LMD_TYPE_FLOAT) {
                double rd = it2d(radix_item);
                if (isnan(rd)) radix = 0; // will trigger RangeError
                else radix = (int)rd;
            }
            // v20: RangeError for invalid radix
            if (radix < 2 || radix > 36) {
                return js_throw_range_error("toString() radix must be between 2 and 36");
            }
            if (radix != 10) {
                // v20: Handle float-to-radix conversion (integer + fractional parts)
                TypeId nt = get_type_id(num);
                // Fast path: small non-negative integer with hex radix (0-255)
                // Avoids double conversion and division loop for the common case
                // of n.toString(16) in encoding loops.
                if (radix == 16 && nt == LMD_TYPE_INT) {
                    int64_t iv = it2i(num);
                    if (iv >= 0 && iv <= 0xFF) {
                        static const char hex_lut[513] =
                            "000102030405060708090a0b0c0d0e0f"
                            "101112131415161718191a1b1c1d1e1f"
                            "202122232425262728292a2b2c2d2e2f"
                            "303132333435363738393a3b3c3d3e3f"
                            "404142434445464748494a4b4c4d4e4f"
                            "505152535455565758595a5b5c5d5e5f"
                            "606162636465666768696a6b6c6d6e6f"
                            "707172737475767778797a7b7c7d7e7f"
                            "808182838485868788898a8b8c8d8e8f"
                            "909192939495969798999a9b9c9d9e9f"
                            "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"
                            "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"
                            "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf"
                            "d0d1d2d3d4d5d6d7d8d9dadbdcdddedf"
                            "e0e1e2e3e4e5e6e7e8e9eaebecedeeef"
                            "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff";
                        const char* p = &hex_lut[iv * 2];
                        if (iv < 16) return (Item){.item = s2it(heap_create_name(p + 1, 1))};
                        return (Item){.item = s2it(heap_create_name(p, 2))};
                    }
                }
                double dval = 0;
                if (nt == LMD_TYPE_INT) dval = (double)it2i(num);
                else if (nt == LMD_TYPE_FLOAT) dval = it2d(num);
                // Handle NaN/Infinity for non-10 radix
                if (isnan(dval)) return (Item){.item = s2it(heap_create_name("NaN", 3))};
                if (isinf(dval)) return dval > 0
                    ? (Item){.item = s2it(heap_create_name("Infinity", 8))}
                    : (Item){.item = s2it(heap_create_name("-Infinity", 9))};
                bool negative = dval < 0;
                if (negative) dval = -dval;

                // Integer part
                uint64_t int_part = (uint64_t)dval;
                double frac_part = dval - (double)int_part;

                const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
                char buf[256];
                int pos = 128;
                buf[pos] = '\0';

                // Convert integer part
                if (int_part == 0) {
                    buf[--pos] = '0';
                } else {
                    while (int_part > 0) {
                        buf[--pos] = digits[int_part % radix];
                        int_part /= radix;
                    }
                }
                if (negative) buf[--pos] = '-';

                // Convert fractional part
                if (frac_part > 0) {
                    int frac_pos = 128;
                    buf[frac_pos++] = '.';
                    // Emit up to 20 fractional digits (sufficient precision)
                    int max_frac = 20;
                    for (int i = 0; i < max_frac && frac_part > 0; i++) {
                        frac_part *= radix;
                        int digit = (int)frac_part;
                        buf[frac_pos++] = digits[digit];
                        frac_part -= digit;
                        // Stop if remaining fraction is negligible
                        if (frac_part < 1e-15) break;
                    }
                    buf[frac_pos] = '\0';
                    return (Item){.item = s2it(heap_create_name(&buf[pos], frac_pos - pos))};
                }
                return (Item){.item = s2it(heap_create_name(&buf[pos], 128 - pos))};
            }
        }
        return js_to_string(num);
    }

    if (method->len == 7 && strncmp(method->chars, "valueOf", 7) == 0) {
        return num;
    }
    if (method->len == 11 && strncmp(method->chars, "toPrecision", 11) == 0) {
        if (argc < 1 || get_type_id(args[0]) == LMD_TYPE_UNDEFINED) return js_to_string(num);
        // per spec ES §21.1.3.5 step 3: ToNumber(precision) must throw TypeError on Symbol
        if (js_key_is_symbol_c(args[0])) {
            return js_throw_type_error("Cannot convert a Symbol value to a number");
        }
        // per spec: step 3 ToInteger(precision) before step 4 NaN/Infinity check
        Item prec_item = js_to_number(args[0]);
        if (js_check_exception()) return ItemNull;
        int precision = 1;
        TypeId pt = get_type_id(prec_item);
        if (pt == LMD_TYPE_INT) precision = (int)it2i(prec_item);
        else if (pt == LMD_TYPE_FLOAT) precision = (int)it2d(prec_item);
        double d = 0;
        TypeId nt = get_type_id(num);
        if (nt == LMD_TYPE_INT) d = (double)it2i(num);
        else if (nt == LMD_TYPE_FLOAT) d = it2d(num);
        // Handle special cases (after ToInteger per spec)
        if (isnan(d)) return (Item){.item = s2it(heap_create_name("NaN", 3))};
        if (isinf(d)) return d > 0
            ? (Item){.item = s2it(heap_create_name("Infinity", 8))}
            : (Item){.item = s2it(heap_create_name("-Infinity", 9))};
        // Range check after NaN/Infinity per spec
        if (precision < 1 || precision > 100) {
            return js_throw_range_error("toPrecision() argument must be between 1 and 100");
        }
        bool negative = d < 0;
        double abs_d = fabs(d);
        if (abs_d == 0.0) {
            // Handle ±0: 0, 0.0, 0.00, etc.
            char buf[128];
            char* p = buf;
            if (negative) *p++ = '-';
            *p++ = '0';
            if (precision > 1) {
                *p++ = '.';
                for (int i = 1; i < precision; i++) *p++ = '0';
            }
            *p = '\0';
            return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
        }
        int exponent = (int)floor(log10(abs_d));
        char buf[256];
        // Use string-based rounding for all precisions
        char sig[128];
        int sig_len, sig_exp;
        js_round_sig_digits(abs_d, precision, sig, &sig_len, &sig_exp);
        exponent = sig_exp; // may be adjusted by carry

        // Format: choose fixed-point, small-decimal, or exponential
        if (exponent >= 0 && exponent < precision) {
            // Fixed-point: e.g. 123, 1.23, 12.3
            char* p = buf;
            if (negative) *p++ = '-';
            int int_digits = exponent + 1;
            for (int i = 0; i < int_digits && i < sig_len; i++) *p++ = sig[i];
            if (int_digits < sig_len) {
                *p++ = '.';
                for (int i = int_digits; i < sig_len; i++) *p++ = sig[i];
            }
            *p = '\0';
        } else if (exponent < 0 && exponent >= -6) {
            // Small number: 0.00123
            char* p = buf;
            if (negative) *p++ = '-';
            *p++ = '0';
            *p++ = '.';
            for (int i = 0; i < -(exponent + 1); i++) *p++ = '0';
            for (int i = 0; i < sig_len; i++) *p++ = sig[i];
            *p = '\0';
        } else {
            // Exponential: 1.23e+5 or 1.23e-7
            char* p = buf;
            if (negative) *p++ = '-';
            *p++ = sig[0];
            if (sig_len > 1) {
                *p++ = '.';
                for (int i = 1; i < sig_len; i++) *p++ = sig[i];
            }
            snprintf(p, sizeof(buf) - (size_t)(p - buf), "e%c%d",
                exponent >= 0 ? '+' : '-', exponent >= 0 ? exponent : -exponent);
        }
        return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
    }
    if (method->len == 13 && strncmp(method->chars, "toExponential", 13) == 0) {
        // per spec: step 2 ToInteger(fractionDigits) before step 4 NaN check
        bool has_frac = (argc >= 1 && get_type_id(args[0]) != LMD_TYPE_UNDEFINED);
        int frac = 0;
        if (has_frac) {
            // per spec ES §21.1.3.2 step 2: ToNumber(fractionDigits) must throw TypeError on Symbol
            if (js_key_is_symbol_c(args[0])) {
                return js_throw_type_error("Cannot convert a Symbol value to a number");
            }
            Item frac_item = js_to_number(args[0]);
            if (js_check_exception()) return ItemNull;
            TypeId ft = get_type_id(frac_item);
            if (ft == LMD_TYPE_INT) frac = (int)it2i(frac_item);
            else if (ft == LMD_TYPE_FLOAT) frac = (int)it2d(frac_item);
        }
        double d = 0;
        TypeId nt = get_type_id(num);
        if (nt == LMD_TYPE_INT) d = (double)it2i(num);
        else if (nt == LMD_TYPE_FLOAT) d = it2d(num);
        // Handle special cases per spec (after ToInteger)
        if (isnan(d)) return (Item){.item = s2it(heap_create_name("NaN", 3))};
        if (isinf(d)) return d > 0
            ? (Item){.item = s2it(heap_create_name("Infinity", 8))}
            : (Item){.item = s2it(heap_create_name("-Infinity", 9))};
        // Range check after NaN/Infinity per spec
        if (has_frac && (frac < 0 || frac > 100)) {
            return js_throw_range_error("toExponential() argument must be between 0 and 100");
        }
        char buf[256];
        bool negative = d < 0;
        double abs_d = fabs(d);
        if (abs_d == 0.0) {
            // Handle ±0 specially
            if (!has_frac) {
                snprintf(buf, sizeof(buf), "%s0e+0", negative ? "-" : "");
            } else {
                char* p = buf;
                if (negative) *p++ = '-';
                *p++ = '0';
                if (frac > 0) {
                    *p++ = '.';
                    for (int i = 0; i < frac; i++) *p++ = '0';
                }
                snprintf(p, sizeof(buf) - (size_t)(p - buf), "e+0");
            }
        } else {
            // ES spec: find e,n such that n × 10^(e-f) ≈ x, round half up
            int exp = (int)floor(log10(abs_d));
            int digits = has_frac ? frac + 1 : 0;
            if (!has_frac) {
                // No fractionDigits: use shortest unique representation
                // Try increasing precision until round-trip matches
                char* p = buf;
                if (negative) *p++ = '-';
                char tbuf[64];
                int best_frac = 0;
                for (int try_frac = 0; try_frac <= 20; try_frac++) {
                    snprintf(tbuf, sizeof(tbuf), "%.*e", try_frac, abs_d);
                    double roundtrip;
                    sscanf(tbuf, "%lf", &roundtrip);
                    if (roundtrip == abs_d) {
                        best_frac = try_frac;
                        break;
                    }
                    best_frac = try_frac;
                }
                snprintf(p, sizeof(buf) - (size_t)(p - buf), "%.*e", best_frac, abs_d);
            } else {
                // Use string-based rounding for ES-spec round-half-up
                char sig[128];
                int sig_len, sig_exp;
                js_round_sig_digits(abs_d, frac + 1, sig, &sig_len, &sig_exp);
                // Build exponential string
                char* p = buf;
                if (negative) *p++ = '-';
                *p++ = sig[0];
                if (frac > 0) {
                    *p++ = '.';
                    for (int i = 1; i < sig_len && i <= frac; i++) *p++ = sig[i];
                    // Pad with zeros if needed
                    for (int i = sig_len; i <= frac; i++) *p++ = '0';
                }
                snprintf(p, sizeof(buf) - (size_t)(p - buf), "e%c%d",
                    sig_exp >= 0 ? '+' : '-', sig_exp >= 0 ? sig_exp : -sig_exp);
            }
        }
        // Normalize exponent: remove leading zeros (e+07 -> e+7)
        char* e = strchr(buf, 'e');
        if (e) {
            char sign = e[1]; // '+' or '-'
            char* digits_p = e + 2;
            while (*digits_p == '0' && *(digits_p + 1)) digits_p++;
            char norm[16];
            snprintf(norm, sizeof(norm), "e%c%s", sign, digits_p);
            strcpy(e, norm);
        }
        return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
    }

    if (method->len == 14 && strncmp(method->chars, "toLocaleString", 14) == 0) {
        return js_to_string(num);
    }

    log_debug("js_number_method: unknown method '%.*s'", (int)method->len, method->chars);
    return ItemNull;
}

// =============================================================================
// String Methods (v5 additions)
// =============================================================================

extern "C" Item js_string_charCodeAt(Item str_item, Item index_item) {
    String* s = it2s(str_item);
    if (!s) return (Item){.item = i2it(0)};

    int idx = 0;
    TypeId itype = get_type_id(index_item);
    if (itype == LMD_TYPE_INT) {
        idx = (int)it2i(index_item);
    } else if (itype == LMD_TYPE_FLOAT) {
        idx = (int)it2d(index_item);
    }

    if (idx < 0 || idx >= (int)s->len) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    // Return the UTF-16 code unit (for ASCII, same as byte value)
    return (Item){.item = i2it((int64_t)(unsigned char)s->chars[idx])};
}

extern "C" Item js_string_fromCharCode(Item code_item) {
    int code = 0;
    TypeId type = get_type_id(code_item);
    if (type == LMD_TYPE_INT) {
        code = (int)it2i(code_item);
    } else if (type == LMD_TYPE_FLOAT) {
        code = (int)it2d(code_item);
    }
    // JS fromCharCode uses UTF-16 code units (truncate to 16-bit)
    code &= 0xFFFF;

    char buf[5]; // max 4 bytes for UTF-8 + null
    int len = 0;
    if (code < 128) {
        buf[0] = (char)code;
        len = 1;
    } else if (code < 0x800) {
        buf[0] = (char)(0xC0 | (code >> 6));
        buf[1] = (char)(0x80 | (code & 0x3F));
        len = 2;
    } else {
        buf[0] = (char)(0xE0 | (code >> 12));
        buf[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (code & 0x3F));
        len = 3;
    }
    buf[len] = '\0';

    return (Item){.item = s2it(heap_strcpy(buf, len))};
}

// Helper: encode a UTF-16 code unit to UTF-8 into buf, return bytes written
static int encode_charcode_utf8(char* buf, int code) {
    code &= 0xFFFF; // truncate to 16-bit (JS fromCharCode uses UTF-16 code units)
    if (code < 128) {
        buf[0] = (char)code;
        return 1;
    } else if (code < 0x800) {
        buf[0] = (char)(0xC0 | (code >> 6));
        buf[1] = (char)(0x80 | (code & 0x3F));
        return 2;
    } else {
        buf[0] = (char)(0xE0 | (code >> 12));
        buf[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (code & 0x3F));
        return 3;
    }
}

// encode a full Unicode codepoint (up to U+10FFFF) to UTF-8
static int encode_charcode_full_utf8(char* buf, int code) {
    if (code < 0 || code > 0x10FFFF) return 0;
    if (code < 0x80) {
        buf[0] = (char)code;
        return 1;
    } else if (code < 0x800) {
        buf[0] = (char)(0xC0 | (code >> 6));
        buf[1] = (char)(0x80 | (code & 0x3F));
        return 2;
    } else if (code < 0x10000) {
        buf[0] = (char)(0xE0 | (code >> 12));
        buf[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (code & 0x3F));
        return 3;
    } else {
        buf[0] = (char)(0xF0 | (code >> 18));
        buf[1] = (char)(0x80 | ((code >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (code & 0x3F));
        return 4;
    }
}

// Multi-argument String.fromCharCode: js_string_fromCharCode_array(Item arr)
// Takes a Lambda Array or TypedArray of code points and returns a concatenated string
extern "C" Item js_string_fromCharCode_array(Item arr_item) {
    TypeId type = get_type_id(arr_item);

    // Handle TypedArray (Uint8Array, Int32Array, etc.)
    if (type == LMD_TYPE_MAP && js_is_typed_array(arr_item)) {
        JsTypedArray* ta = js_get_typed_array_ptr(arr_item.map);
        int len = ta->length;
        if (len == 0) return (Item){.item = s2it(heap_strcpy("", 0))};
        char* buf = (char*)mem_alloc(len * 4 + 1, MEM_CAT_JS_RUNTIME);
        int pos = 0;
        for (int i = 0; i < len; i++) {
            int code = 0;
            switch (ta->element_type) {
            case JS_TYPED_UINT8:   code = ((uint8_t*)ta->data)[i]; break;
            case JS_TYPED_INT8:    code = ((int8_t*)ta->data)[i]; break;
            case JS_TYPED_UINT16:  code = ((uint16_t*)ta->data)[i]; break;
            case JS_TYPED_INT16:   code = ((int16_t*)ta->data)[i]; break;
            case JS_TYPED_UINT32:  code = (int)((uint32_t*)ta->data)[i]; break;
            case JS_TYPED_INT32:   code = ((int32_t*)ta->data)[i]; break;
            case JS_TYPED_FLOAT32: code = (int)((float*)ta->data)[i]; break;
            case JS_TYPED_FLOAT64: code = (int)((double*)ta->data)[i]; break;
            }
            code &= 0xFFFF;
            // combine adjacent surrogate pairs into a single supplementary codepoint
            if (code >= 0xD800 && code <= 0xDBFF && i + 1 < len) {
                int lo = 0;
                switch (ta->element_type) {
                case JS_TYPED_UINT8:   lo = ((uint8_t*)ta->data)[i+1]; break;
                case JS_TYPED_INT8:    lo = ((int8_t*)ta->data)[i+1]; break;
                case JS_TYPED_UINT16:  lo = ((uint16_t*)ta->data)[i+1]; break;
                case JS_TYPED_INT16:   lo = ((int16_t*)ta->data)[i+1]; break;
                case JS_TYPED_UINT32:  lo = (int)((uint32_t*)ta->data)[i+1]; break;
                case JS_TYPED_INT32:   lo = ((int32_t*)ta->data)[i+1]; break;
                case JS_TYPED_FLOAT32: lo = (int)((float*)ta->data)[i+1]; break;
                case JS_TYPED_FLOAT64: lo = (int)((double*)ta->data)[i+1]; break;
                }
                lo &= 0xFFFF;
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    int cp = 0x10000 + ((code - 0xD800) << 10) + (lo - 0xDC00);
                    pos += encode_charcode_full_utf8(buf + pos, cp);
                    i++; // skip the low surrogate
                    continue;
                }
            }
            pos += encode_charcode_utf8(buf + pos, code);
        }
        buf[pos] = '\0';
        Item result = (Item){.item = s2it(heap_strcpy(buf, pos))};
        mem_free(buf);
        return result;
    }

    if (type != LMD_TYPE_ARRAY) {
        return js_string_fromCharCode(arr_item); // fallback: single arg
    }
    Array* arr = arr_item.array;
    int len = arr->length;
    if (len == 0) return (Item){.item = s2it(heap_strcpy("", 0))};
    char* buf = (char*)mem_alloc(len * 4 + 1, MEM_CAT_JS_RUNTIME);
    int pos = 0;
    for (int i = 0; i < len; i++) {
        int code = 0;
        TypeId itype = get_type_id(arr->items[i]);
        if (itype == LMD_TYPE_INT) {
            code = (int)it2i(arr->items[i]);
        } else if (itype == LMD_TYPE_FLOAT) {
            code = (int)it2d(arr->items[i]);
        }
        code &= 0xFFFF;
        // combine adjacent surrogate pairs into a single supplementary codepoint
        if (code >= 0xD800 && code <= 0xDBFF && i + 1 < len) {
            int lo = 0;
            TypeId lo_type = get_type_id(arr->items[i + 1]);
            if (lo_type == LMD_TYPE_INT) lo = (int)it2i(arr->items[i + 1]);
            else if (lo_type == LMD_TYPE_FLOAT) lo = (int)it2d(arr->items[i + 1]);
            lo &= 0xFFFF;
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                int cp = 0x10000 + ((code - 0xD800) << 10) + (lo - 0xDC00);
                pos += encode_charcode_full_utf8(buf + pos, cp);
                i++; // skip the low surrogate
                continue;
            }
        }
        pos += encode_charcode_utf8(buf + pos, code);
    }
    buf[pos] = '\0';
    Item result = (Item){.item = s2it(heap_strcpy(buf, pos))};
    mem_free(buf);
    return result;
}

// Helper: encode a full Unicode code point to UTF-8 (up to 4 bytes)
static int encode_codepoint_utf8(char* buf, int code) {
    if (code < 0 || code > 0x10FFFF) return 0;
    if (code < 0x80) {
        buf[0] = (char)code;
        return 1;
    } else if (code < 0x800) {
        buf[0] = (char)(0xC0 | (code >> 6));
        buf[1] = (char)(0x80 | (code & 0x3F));
        return 2;
    } else if (code < 0x10000) {
        buf[0] = (char)(0xE0 | (code >> 12));
        buf[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (code & 0x3F));
        return 3;
    } else {
        buf[0] = (char)(0xF0 | (code >> 18));
        buf[1] = (char)(0x80 | ((code >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (code & 0x3F));
        return 4;
    }
}

// String.fromCodePoint(cp) — single code point
extern "C" Item js_string_fromCodePoint(Item code_item) {
    int code = 0;
    TypeId type = get_type_id(code_item);
    if (type == LMD_TYPE_INT) {
        code = (int)it2i(code_item);
    } else if (type == LMD_TYPE_FLOAT) {
        code = (int)it2d(code_item);
    }
    char buf[5];
    int len = encode_codepoint_utf8(buf, code);
    buf[len] = '\0';
    return (Item){.item = s2it(heap_strcpy(buf, len))};
}

// String.fromCodePoint(cp1, cp2, ...) — multiple code points via array
extern "C" Item js_string_fromCodePoint_array(Item arr_item) {
    TypeId type = get_type_id(arr_item);
    if (type != LMD_TYPE_ARRAY) {
        return js_string_fromCodePoint(arr_item);
    }
    Array* arr = arr_item.array;
    int len = arr->length;
    if (len == 0) return (Item){.item = s2it(heap_strcpy("", 0))};
    char* buf = (char*)mem_alloc(len * 4 + 1, MEM_CAT_JS_RUNTIME);
    int pos = 0;
    for (int i = 0; i < len; i++) {
        int code = 0;
        TypeId itype = get_type_id(arr->items[i]);
        if (itype == LMD_TYPE_INT) {
            code = (int)it2i(arr->items[i]);
        } else if (itype == LMD_TYPE_FLOAT) {
            code = (int)it2d(arr->items[i]);
        }
        pos += encode_codepoint_utf8(buf + pos, code);
    }
    buf[pos] = '\0';
    Item result = (Item){.item = s2it(heap_strcpy(buf, pos))};
    mem_free(buf);
    return result;
}

// String.raw(template, ...substitutions) — tagged template literal
// Called with args[0]=template_object (has .raw property), args[1..]=substitutions
extern "C" Item js_string_raw(Item* args, int argc) {
    if (argc < 1) return (Item){.item = s2it(heap_strcpy("", 0))};

    Item template_obj = args[0];
    // Get template.raw
    Item raw_key = (Item){.item = s2it(heap_create_name("raw", 3))};
    Item raw = js_property_access(template_obj, raw_key);
    if (raw.item == ITEM_NULL || raw.item == ITEM_JS_UNDEFINED) {
        return (Item){.item = s2it(heap_strcpy("", 0))};
    }

    // Get raw.length (may be a MAP with numeric keys + length property)
    Item len_key = (Item){.item = s2it(heap_create_name("length", 6))};
    Item len_item = js_property_access(raw, len_key);
    int raw_len = 0;
    TypeId len_type = get_type_id(len_item);
    if (len_type == LMD_TYPE_INT) raw_len = (int)it2i(len_item);
    else if (len_type == LMD_TYPE_FLOAT) raw_len = (int)it2d(len_item);
    if (raw_len <= 0) return (Item){.item = s2it(heap_strcpy("", 0))};

    StrBuf* buf = strbuf_new();
    for (int i = 0; i < raw_len; i++) {
        // Get raw[i] — use string key for MAP compatibility
        char idx_buf[16];
        int idx_len = snprintf(idx_buf, sizeof(idx_buf), "%d", i);
        Item idx = (Item){.item = s2it(heap_create_name(idx_buf, idx_len))};
        Item part = js_property_access(raw, idx);
        Item part_str = js_to_string(part);
        String* s = it2s(part_str);
        if (s && s->len > 0) strbuf_append_str_n(buf, s->chars, s->len);

        // Interleave substitution if present (i+1 in args because args[0] is template)
        if (i < argc - 1) {
            Item sub_str = js_to_string(args[i + 1]);
            String* sub_s = it2s(sub_str);
            if (sub_s && sub_s->len > 0) strbuf_append_str_n(buf, sub_s->chars, sub_s->len);
        }
    }
    String* result = heap_strcpy(buf->str, buf->length);
    strbuf_free(buf);
    return (Item){.item = s2it(result)};
}

// =============================================================================
// Console output helpers
// =============================================================================
extern "C" Item js_property_get_str(Item object, const char* key, int key_len);
extern Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);

extern "C" void js_console_write_to_stdout(const char* data, int len) {
    fwrite(data, 1, len, stdout);
    fflush(stdout);
}

extern "C" void js_console_write_to_stderr(const char* data, int len) {
    fwrite(data, 1, len, stderr);
    fflush(stderr);
}

// =============================================================================
// Console multi-argument log
// =============================================================================

// forward declaration for util.format
extern "C" Item js_util_format(Item args_item);

// check if a string contains printf-style format specifiers
static bool has_format_specifiers(String* s) {
    for (int i = 0; i < (int)s->len - 1; i++) {
        if (s->chars[i] == '%') {
            char next = s->chars[i + 1];
            if (next == 's' || next == 'd' || next == 'i' || next == 'f' ||
                next == 'j' || next == 'o' || next == 'O') {
                return true;
            }
        }
    }
    return false;
}

extern "C" void js_console_log_multi(Item* args, int argc) {
    // if first arg is a string with format specifiers and there are more args,
    // use util.format-style substitution (matches Node.js console.log behavior)
    char buf[4096];
    int pos = 0;
    if (argc >= 2 && get_type_id(args[0]) == LMD_TYPE_STRING &&
        has_format_specifiers(it2s(args[0]))) {
        Item arr = js_array_new(0);
        for (int i = 0; i < argc; i++) {
            js_array_push(arr, args[i]);
        }
        Item formatted = js_util_format(arr);
        if (get_type_id(formatted) == LMD_TYPE_STRING) {
            String* s = it2s(formatted);
            if (s && s->len > 0) {
                int copy = (int)s->len < (int)sizeof(buf) - 1 ? (int)s->len : (int)sizeof(buf) - 1;
                memcpy(buf, s->chars, copy);
                pos = copy;
            }
        }
    } else {
        for (int i = 0; i < argc; i++) {
            if (i > 0 && pos < (int)sizeof(buf) - 1) buf[pos++] = ' ';
            Item str = js_to_string(args[i]);
            String* s = it2s(str);
            if (s && s->len > 0) {
                int copy = (int)s->len < (int)sizeof(buf) - 1 - pos ? (int)s->len : (int)sizeof(buf) - 1 - pos;
                memcpy(buf + pos, s->chars, copy);
                pos += copy;
            }
        }
    }
    if (pos < (int)sizeof(buf) - 1) buf[pos++] = '\n';
    js_console_write_to_stdout(buf, pos);
}

// =============================================================================
// Console stub methods (count, clear, group, time, trace, assert, dir, table)
// =============================================================================

// console.count / console.countReset
static int js_console_count_map[64] = {0};
static uint32_t js_console_count_keys[64] = {0};
static int js_console_count_used = 0;

static int* js_console_count_slot(const char* label, int label_len) {
    uint32_t h = 0;
    for (int i = 0; i < label_len; i++) h = h * 31 + (uint8_t)label[i];
    for (int i = 0; i < js_console_count_used; i++) {
        if (js_console_count_keys[i] == h) return &js_console_count_map[i];
    }
    if (js_console_count_used < 64) {
        int idx = js_console_count_used++;
        js_console_count_keys[idx] = h;
        js_console_count_map[idx] = 0;
        return &js_console_count_map[idx];
    }
    return &js_console_count_map[0]; // fallback
}

extern "C" Item js_console_count_fn(Item label_item) {
    const char* label = "default";
    int label_len = 7;
    char buf[256];
    char label_buf[128];
    TypeId lt = get_type_id(label_item);
    if (lt == LMD_TYPE_STRING) {
        String* s = it2s(label_item);
        if (s && s->len > 0) { label = s->chars; label_len = (int)s->len; }
    } else if (lt != LMD_TYPE_UNDEFINED) {
        // convert non-string, non-undefined to string
        Item str = js_to_string(label_item);
        String* s = it2s(str);
        if (s && s->len > 0) {
            int copy = (int)s->len < (int)sizeof(label_buf) - 1 ? (int)s->len : (int)sizeof(label_buf) - 1;
            memcpy(label_buf, s->chars, copy);
            label_buf[copy] = '\0';
            label = label_buf;
            label_len = copy;
        }
    }
    int* slot = js_console_count_slot(label, label_len);
    (*slot)++;
    int n = snprintf(buf, sizeof(buf), "%.*s: %d\n", label_len, label, *slot);
    js_console_write_to_stdout(buf, n);
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_console_countReset_fn(Item label_item) {
    const char* label = "default";
    int label_len = 7;
    char label_buf[128];
    TypeId lt = get_type_id(label_item);
    if (lt == LMD_TYPE_STRING) {
        String* s = it2s(label_item);
        if (s && s->len > 0) { label = s->chars; label_len = (int)s->len; }
    } else if (lt != LMD_TYPE_UNDEFINED) {
        Item str = js_to_string(label_item);
        String* s = it2s(str);
        if (s && s->len > 0) {
            int copy = (int)s->len < (int)sizeof(label_buf) - 1 ? (int)s->len : (int)sizeof(label_buf) - 1;
            memcpy(label_buf, s->chars, copy);
            label_buf[copy] = '\0';
            label = label_buf;
            label_len = copy;
        }
    }
    int* slot = js_console_count_slot(label, label_len);
    *slot = 0;
    return (Item){.item = ITEM_JS_UNDEFINED};
}

// console.time / console.timeEnd / console.timeLog
static double js_console_timers[32] = {0};
static uint32_t js_console_timer_keys[32] = {0};
static int js_console_timer_used = 0;

static int js_console_timer_find(uint32_t h) {
    for (int i = 0; i < js_console_timer_used; i++) {
        if (js_console_timer_keys[i] == h) return i;
    }
    return -1;
}

extern "C" Item js_console_time_fn(Item label_item) {
    const char* label = "default";
    int label_len = 7;
    if (get_type_id(label_item) == LMD_TYPE_STRING) {
        String* s = it2s(label_item);
        if (s && s->len > 0) { label = s->chars; label_len = (int)s->len; }
    }
    uint32_t h = 0;
    for (int i = 0; i < label_len; i++) h = h * 31 + (uint8_t)label[i];
    int idx = js_console_timer_find(h);
    if (idx < 0 && js_console_timer_used < 32) {
        idx = js_console_timer_used++;
        js_console_timer_keys[idx] = h;
    }
    if (idx >= 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        js_console_timers[idx] = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_console_timeEnd_fn(Item label_item) {
    const char* label = "default";
    int label_len = 7;
    if (get_type_id(label_item) == LMD_TYPE_STRING) {
        String* s = it2s(label_item);
        if (s && s->len > 0) { label = s->chars; label_len = (int)s->len; }
    }
    uint32_t h = 0;
    for (int i = 0; i < label_len; i++) h = h * 31 + (uint8_t)label[i];
    int idx = js_console_timer_find(h);
    if (idx >= 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        double now = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
        double elapsed = now - js_console_timers[idx];
        char buf[256];
        int n = snprintf(buf, sizeof(buf), "%.*s: %.3fms\n", label_len, label, elapsed);
        js_console_write_to_stdout(buf, n);
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_console_timeLog_fn(Item label_item) {
    return js_console_timeEnd_fn(label_item); // same output, but doesn't clear timer
}

// console.clear — sends escape sequence when TTY, through process.stdout.write
extern "C" Item js_console_clear_fn(void) {
    // check process.stdout.isTTY
    extern Item js_get_process_object_value(void);
    Item process = js_get_process_object_value();
    if (process.item != ITEM_NULL) {
        Item stdout_obj = js_property_get_str(process, "stdout", 6);
        if (stdout_obj.item != ITEM_NULL && get_type_id(stdout_obj) != LMD_TYPE_UNDEFINED) {
            Item isTTY = js_property_get_str(stdout_obj, "isTTY", 5);
            extern bool js_is_truthy(Item val);
            if (js_is_truthy(isTTY)) {
                // ESC[1;1H ESC[0J — move cursor to 1,1 and clear screen down
                js_console_write_to_stdout("\x1b[1;1H\x1b[0J", 10);
            }
        }
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

// console.group / console.groupEnd — stubs
static int js_console_group_depth = 0;
extern "C" Item js_console_group_fn(Item label_item) {
    if (get_type_id(label_item) == LMD_TYPE_STRING) {
        String* s = it2s(label_item);
        if (s && s->len > 0) {
            char buf[4096];
            int n = snprintf(buf, sizeof(buf), "%.*s\n", (int)s->len, s->chars);
            js_console_write_to_stdout(buf, n);
        }
    }
    js_console_group_depth++;
    return (Item){.item = ITEM_JS_UNDEFINED};
}

extern "C" Item js_console_groupEnd_fn(void) {
    if (js_console_group_depth > 0) js_console_group_depth--;
    return (Item){.item = ITEM_JS_UNDEFINED};
}

// console.trace — print stack trace stub
extern "C" Item js_console_trace_fn(Item label_item) {
    char buf[256];
    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - n, "Trace");
    if (get_type_id(label_item) == LMD_TYPE_STRING) {
        String* s = it2s(label_item);
        if (s && s->len > 0) n += snprintf(buf + n, sizeof(buf) - n, ": %.*s", (int)s->len, s->chars);
    }
    n += snprintf(buf + n, sizeof(buf) - n, "\n");
    js_console_write_to_stdout(buf, n);
    return (Item){.item = ITEM_JS_UNDEFINED};
}

// console.dir — uses util.inspect-like output
extern "C" Item js_console_dir_fn(Item obj) {
    extern void js_console_log(Item value);
    js_console_log(obj);
    return (Item){.item = ITEM_JS_UNDEFINED};
}

// console.table — simplified stub, just logs the value
extern "C" Item js_console_table_fn(Item data) {
    extern void js_console_log(Item value);
    js_console_log(data);
    return (Item){.item = ITEM_JS_UNDEFINED};
}

// console.assert(value, ...args) — if !value, print assertion failed
extern "C" bool js_is_truthy(Item val);
extern "C" Item js_console_assert_fn(Item cond, Item msg) {
    if (!js_is_truthy(cond)) {
        char buf[4096];
        int n = snprintf(buf, sizeof(buf), "Assertion failed");
        if (get_type_id(msg) == LMD_TYPE_STRING) {
            String* s = it2s(msg);
            if (s && s->len > 0) n += snprintf(buf + n, sizeof(buf) - n, ": %.*s", (int)s->len, s->chars);
        }
        n += snprintf(buf + n, sizeof(buf) - n, "\n");
        js_console_write_to_stderr(buf, n);
    }
    return (Item){.item = ITEM_JS_UNDEFINED};
}

// =============================================================================
// Array fill (regular arrays)
// =============================================================================

extern "C" Item js_array_fill(Item arr_item, Item value) {
    TypeId type = get_type_id(arr_item);

    // Check if typed array first
    if (type == LMD_TYPE_MAP && js_is_typed_array(arr_item)) {
        return js_typed_array_fill(arr_item, value, 0, INT_MAX);
    }

    if (type != LMD_TYPE_ARRAY) return arr_item;

    int len = fn_len(arr_item);
    Array* arr = it2arr(arr_item);
    for (int i = 0; i < len; i++) {
        fn_array_set(arr, i, value);
    }

    return arr_item;
}

// =============================================================================
// instanceof operator — walks prototype chain
// =============================================================================

// partial JsFunction layout for accessing function name (used by instanceof fallback)
struct JsFuncName {
    TypeId type_id;
    void* func_ptr;
    int param_count;
    Item* env;
    int env_size;
    Item prototype;
    Item bound_this;
    Item* bound_args;
    int bound_argc;
    String* name;
};

static Item js_instanceof_impl(Item left, Item right, bool skip_symbol);
extern "C" Item js_array_get_custom_proto(Item arr);

extern "C" Item js_instanceof(Item left, Item right) {
    return js_instanceof_impl(left, right, false);
}

// OrdinaryHasInstance — same as instanceof but skips Symbol.hasInstance check
extern "C" Item js_ordinary_has_instance(Item left, Item right) {
    return js_instanceof_impl(left, right, true);
}

static Item js_instanceof_impl(Item left, Item right, bool skip_symbol) {
    // right should be a constructor (a class). We check if left's prototype chain
    // contains right's prototype. For our implementation, we check if right has
    // a __class_name__ marker that matches any __class_name__ in left's proto chain.

    // ES spec: if right is not an object, throw TypeError
    TypeId rt = get_type_id(right);
    if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_FUNC) {
        js_throw_type_error("Right-hand side of 'instanceof' is not an object");
        return (Item){.item = b2it(false)};
    }



    // v16: Check for Symbol.hasInstance on the right-hand constructor FIRST (before type check)
    // Per ES spec §7.3.21: if right[@@hasInstance] exists, call it
    if (!skip_symbol) {
        if (rt == LMD_TYPE_MAP || rt == LMD_TYPE_FUNC) {
            // look for __sym_3 (Symbol.hasInstance = ID 3) via property_get (handles both MAP and FUNC)
            Item sym_key = (Item){.item = s2it(heap_create_name("__sym_3", 7))};
            Item has_instance_fn = js_property_get(right, sym_key);
            // ES §12.10.4 step 3: ReturnIfAbrupt — propagate getter errors
            if (js_check_exception()) return ItemNull;
            if (has_instance_fn.item != ItemNull.item && get_type_id(has_instance_fn) == LMD_TYPE_FUNC) {
                Item args[1] = { left };
                Item result = js_call_function(has_instance_fn, right, args, 1);
                return (Item){.item = b2it(js_is_truthy(result))};
            }
        }
    }

    // ES spec: if right is an object but not callable, throw TypeError
    if (rt == LMD_TYPE_MAP) {
        // MAP is only valid if it has __class_name__ (constructor object) — otherwise not callable
        Item class_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
        Item cn = map_get(right.map, class_key);
        if (cn.item == 0 || get_type_id(cn) != LMD_TYPE_STRING) {
            js_throw_type_error("Right-hand side of 'instanceof' is not callable");
            return (Item){.item = b2it(false)};
        }
    }

    if (get_type_id(left) != LMD_TYPE_MAP && get_type_id(left) != LMD_TYPE_ARRAY && get_type_id(left) != LMD_TYPE_FUNC) return (Item){.item = b2it(false)};

    // If right is a function, use ES spec OrdinaryHasInstance:
    // Walk left's __proto__ chain comparing against right.prototype
    TypeId right_type = get_type_id(right);
    if (right_type == LMD_TYPE_FUNC) {
        // v20: Get Func.prototype via property access (handles both Function and JsFunction)
        Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
        Item func_proto = js_property_get(right, proto_key);
        // ES spec 7.3.19 step 6: If Type(P) is not Object, throw TypeError
        TypeId fp_type = get_type_id(func_proto);
        if (fp_type != LMD_TYPE_MAP && fp_type != LMD_TYPE_ARRAY && fp_type != LMD_TYPE_FUNC) {
            js_throw_type_error("Function has non-object prototype in instanceof check");
            return (Item){.item = b2it(false)};
        }
        if (func_proto.item != ItemNull.item && fp_type == LMD_TYPE_MAP) {
            // Walk left's __proto__ chain looking for func_proto (identity check)
            // Use js_get_prototype_of for each step (handles arrays, functions, builtins)
            Item obj = js_get_prototype_of(left);
            int depth = 0;
            while (obj.item != 0 && obj.item != ItemNull.item && depth < 32) {
                TypeId ot = get_type_id(obj);
                if (ot == LMD_TYPE_MAP && obj.map == func_proto.map) {
                    return (Item){.item = b2it(true)};
                }
                obj = js_get_prototype_of(obj);
                depth++;
            }
        }
        // Fallback: also check __ctor__ walk for class-based objects
        {
            Item obj = left;
            int depth = 0;
            Item ctor_key = (Item){.item = s2it(heap_create_name("__ctor__", 8))};
            Item proto_key_item = (Item){.item = s2it(heap_create_name("__proto__", 9))};
            while (obj.item != 0 && get_type_id(obj) == LMD_TYPE_MAP && depth < 32) {
                Item ctor_val = map_get(obj.map, ctor_key);
                if (ctor_val.item != 0 && get_type_id(ctor_val) == LMD_TYPE_FUNC) {
                    if (ctor_val.item == right.item) return (Item){.item = b2it(true)};
                }
                obj = map_get(obj.map, proto_key_item);
                depth++;
            }
        }
        // v18c: name-based fallback for built-in constructors
        JsFuncName* fp = (JsFuncName*)right.function;
        if (fp->name && fp->name->len > 0) {
            Item name_item = (Item){.item = s2it(fp->name)};
            return js_instanceof_classname(left, name_item);
        }
        return (Item){.item = b2it(false)};
    }

    if (right_type != LMD_TYPE_MAP) return (Item){.item = b2it(false)};

    // Get the class name from right (constructor's __class_name__)
    Item class_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
    Item right_name = map_get(right.map, class_key);
    if (right_name.item == 0 || get_type_id(right_name) != LMD_TYPE_STRING)
        return (Item){.item = b2it(false)};

    // Delegate to name-based check
    return js_instanceof_classname(left, right_name);
}

// Forward decls for DOM identity checks (host objects under MAP_KIND_DOM that
// don't carry __class_name__ in their property map).
extern "C" bool js_dom_item_is_range(Item item);
extern "C" bool js_dom_item_is_selection(Item item);

// instanceof check by class name string — walks prototype chain checking __class_name__
extern "C" Item js_instanceof_classname(Item left, Item classname) {
    if (get_type_id(classname) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};

    String* rn = it2s(classname);
    if (!rn) return (Item){.item = b2it(false)};

    // Check built-in types that don't use __class_name__ prototype chain
    TypeId lt = get_type_id(left);

    // DOM host-object identity checks (Selection/Range wrappers are
    // MAP_KIND_DOM with no __class_name__ in their property map).
    if (rn->len == 9 && strncmp(rn->chars, "Selection", 9) == 0) {
        return (Item){.item = b2it(js_dom_item_is_selection(left))};
    }
    if (rn->len == 5 && strncmp(rn->chars, "Range", 5) == 0) {
        return (Item){.item = b2it(js_dom_item_is_range(left))};
    }

    // Array check
    if (rn->len == 5 && strncmp(rn->chars, "Array", 5) == 0) {
        return (Item){.item = b2it(lt == LMD_TYPE_ARRAY)};
    }
    // v20: Object check — any object type is instanceof Object (unless null-prototype)
    if (rn->len == 6 && strncmp(rn->chars, "Object", 6) == 0) {
        if (lt == LMD_TYPE_ARRAY || lt == LMD_TYPE_FUNC) return (Item){.item = b2it(true)};
        if (lt == LMD_TYPE_MAP) {
            // null-prototype objects (Object.create(null)) are NOT instanceof Object
            // js_get_prototype stores undefined as sentinel for null prototype
            Item raw_proto = js_get_prototype(left);
            if (raw_proto.item == ITEM_JS_UNDEFINED) {
                return (Item){.item = b2it(false)};
            }
            return (Item){.item = b2it(true)};
        }
        return (Item){.item = b2it(false)};
    }
    // v20: Function check — any function is instanceof Function
    if (rn->len == 8 && strncmp(rn->chars, "Function", 8) == 0) {
        return (Item){.item = b2it(lt == LMD_TYPE_FUNC)};
    }
    // RegExp check
    if (rn->len == 6 && strncmp(rn->chars, "RegExp", 6) == 0) {
        // RegExp objects are stored as maps with a regex data pointer — check for __rd property
        if (lt == LMD_TYPE_MAP) {
            Item rkey = (Item){.item = s2it(heap_create_name("__rd", 4))};
            Item rval = map_get(left.map, rkey);
            return (Item){.item = b2it(rval.item != 0 && get_type_id(rval) != LMD_TYPE_NULL)};
        }
        return (Item){.item = b2it(false)};
    }

    if (lt == LMD_TYPE_MAP) {
        // Collection types: WeakSet, WeakMap, Map, Set
        if ((rn->len == 3 && strncmp(rn->chars, "Map", 3) == 0) ||
            (rn->len == 7 && strncmp(rn->chars, "WeakMap", 7) == 0)) {
            return (Item){.item = b2it(js_is_map_instance(left))};
        }
        if ((rn->len == 3 && strncmp(rn->chars, "Set", 3) == 0) ||
            (rn->len == 7 && strncmp(rn->chars, "WeakSet", 7) == 0)) {
            return (Item){.item = b2it(js_is_set_instance(left))};
        }

        // TypedArray types
        if (js_is_typed_array(left)) {
            JsTypedArray* ta = js_get_typed_array_ptr(left.map);
            if (rn->len == 10 && strncmp(rn->chars, "Uint8Array", 10) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_UINT8)};
            if (rn->len == 17 && strncmp(rn->chars, "Uint8ClampedArray", 17) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_UINT8_CLAMPED)};
            if (rn->len == 11 && strncmp(rn->chars, "Uint16Array", 11) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_UINT16)};
            if (rn->len == 11 && strncmp(rn->chars, "Uint32Array", 11) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_UINT32)};
            if (rn->len == 9 && strncmp(rn->chars, "Int8Array", 9) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_INT8)};
            if (rn->len == 10 && strncmp(rn->chars, "Int16Array", 10) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_INT16)};
            if (rn->len == 10 && strncmp(rn->chars, "Int32Array", 10) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_INT32)};
            if (rn->len == 12 && strncmp(rn->chars, "Float32Array", 12) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_FLOAT32)};
            if (rn->len == 12 && strncmp(rn->chars, "Float64Array", 12) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_FLOAT64)};
            // BigInt64Array/BigUint64Array — not supported, always false
            if (rn->len >= 12 && strncmp(rn->chars, "Big", 3) == 0)
                return (Item){.item = b2it(false)};
        }

        // ArrayBuffer check
        if (rn->len == 11 && strncmp(rn->chars, "ArrayBuffer", 11) == 0) {
            return (Item){.item = b2it(js_is_arraybuffer(left))};
        }
    }

    if (lt != LMD_TYPE_MAP) {
        // For Arrays with custom prototypes, walk the proto chain looking for class_name
        if (lt == LMD_TYPE_ARRAY) {
            Item custom_proto = js_array_get_custom_proto(left);
            if (custom_proto.item != ItemNull.item && get_type_id(custom_proto) == LMD_TYPE_MAP) {
                Item class_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
                Item obj = custom_proto;
                int depth = 0;
                while (obj.item != 0 && get_type_id(obj) == LMD_TYPE_MAP && depth < 32) {
                    // T5a: prefer typed JsClass byte; fall back to legacy string
                    const char* on_chars = NULL;
                    int on_len = 0;
                    JsClass jc = js_class_get(obj);
                    if (jc != JS_CLASS_NONE) {
                        const char* nm = js_class_to_name(jc);
                        if (nm) { on_chars = nm; on_len = (int)strlen(nm); }
                    }
                    if (!on_chars) {
                        Item obj_name = map_get(obj.map, class_key);
                        if (obj_name.item != 0 && get_type_id(obj_name) == LMD_TYPE_STRING) {
                            String* on = it2s(obj_name);
                            if (on) { on_chars = on->chars; on_len = (int)on->len; }
                        }
                    }
                    if (on_chars && on_len == rn->len && strncmp(on_chars, rn->chars, on_len) == 0) {
                        return (Item){.item = b2it(true)};
                    }
                    Item proto_key = (Item){.item = s2it(heap_create_name("__proto__", 9))};
                    obj = map_get(obj.map, proto_key);
                    depth++;
                }
            }
        }
        return (Item){.item = b2it(false)};
    }

    Item class_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};

    // Check if left has this class name in its prototype chain
    Item obj = left;
    int depth = 0;
    while (obj.item != 0 && get_type_id(obj) == LMD_TYPE_MAP && depth < 32) {
        // T5a: prefer typed JsClass byte; fall back to legacy `__class_name__` string.
        // (User-defined classes & unenumerated subclasses still use the string.)
        const char* on_chars = NULL;
        int on_len = 0;
        JsClass jc = js_class_get(obj);
        if (jc != JS_CLASS_NONE) {
            const char* nm = js_class_to_name(jc);
            if (nm) { on_chars = nm; on_len = (int)strlen(nm); }
        }
        if (!on_chars) {
            Item obj_name = map_get(obj.map, class_key);
            if (obj_name.item != 0 && get_type_id(obj_name) == LMD_TYPE_STRING) {
                String* on = it2s(obj_name);
                if (on) { on_chars = on->chars; on_len = (int)on->len; }
            }
        }
        if (on_chars) {
            if (on_len == rn->len && strncmp(on_chars, rn->chars, on_len) == 0) {
                return (Item){.item = b2it(true)};
            }
            // error hierarchy: any *Error subtype instanceof Error
            if (rn->len == 5 && strncmp(rn->chars, "Error", 5) == 0) {
                // check if class name ends with "Error" (covers TypeError, RangeError, AssertionError, etc.)
                if (on_len > 5 && strncmp(on_chars + on_len - 5, "Error", 5) == 0) {
                    return (Item){.item = b2it(true)};
                }
            }
            // Event hierarchy: any *Event subtype instanceof Event
            if (rn->len == 5 && strncmp(rn->chars, "Event", 5) == 0) {
                if (on_len > 5 && strncmp(on_chars + on_len - 5, "Event", 5) == 0) {
                    return (Item){.item = b2it(true)};
                }
            }
            // UIEvent hierarchy: Mouse / Keyboard / Focus / Composition /
            // Input / Wheel / Pointer events all extend UIEvent.
            if (rn->len == 7 && strncmp(rn->chars, "UIEvent", 7) == 0) {
                static const char* const ui_subs[] = {
                    "MouseEvent", "KeyboardEvent", "FocusEvent",
                    "CompositionEvent", "InputEvent", "WheelEvent",
                    "PointerEvent", NULL };
                for (int i = 0; ui_subs[i]; i++) {
                    size_t sl = strlen(ui_subs[i]);
                    if ((size_t)on_len == sl && strncmp(on_chars, ui_subs[i], sl) == 0) {
                        return (Item){.item = b2it(true)};
                    }
                }
            }
            // MouseEvent hierarchy: Wheel / Pointer extend MouseEvent.
            if (rn->len == 10 && strncmp(rn->chars, "MouseEvent", 10) == 0) {
                if ((on_len == 10 && strncmp(on_chars, "WheelEvent", 10) == 0) ||
                    (on_len == 12 && strncmp(on_chars, "PointerEvent", 12) == 0)) {
                    return (Item){.item = b2it(true)};
                }
            }
            // EventTarget: any DOM Node, document, or constructed EventTarget
            // is an EventTarget. Plain Event instances are NOT.
            if (rn->len == 11 && strncmp(rn->chars, "EventTarget", 11) == 0) {
                if (on_len == 11 && strncmp(on_chars, "EventTarget", 11) == 0) {
                    return (Item){.item = b2it(true)};
                }
            }
        }
        // walk up __proto__
        Item proto_key = (Item){.item = s2it(heap_create_name("__proto__", 9))};
        obj = map_get(obj.map, proto_key);
        depth++;
    }
    return (Item){.item = b2it(false)};
}

// =============================================================================
// in operator — check if key exists in object/array
// =============================================================================

extern "C" Item js_in(Item key, Item object) {
    TypeId type = get_type_id(object);
    // ES spec: TypeError if RHS is not an object
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_ARRAY && type != LMD_TYPE_FUNC
        && type != LMD_TYPE_ELEMENT) {
        return js_throw_type_error("Cannot use 'in' operator to search for a property in a non-object");
    }
    // Document proxy / foreign-document wrappers: properties are served by
    // custom get_property handlers (not stored in the underlying map). Use
    // js_property_get to detect existence — anything other than ItemNull /
    // undefined counts as present.
    if (type == LMD_TYPE_MAP &&
        (object.map->map_kind == MAP_KIND_DOC_PROXY ||
         object.map->map_kind == MAP_KIND_FOREIGN_DOC ||
         object.map->map_kind == MAP_KIND_DOM)) {
        Item v = js_property_get(object, key);
        if (v.item != ItemNull.item && v.item != ITEM_JS_UNDEFINED) {
            return (Item){.item = b2it(true)};
        }
        // Fall through so prototype-chain methods (Object.prototype.toString,
        // hasOwnProperty, etc.) still resolve true.
    }
    // Proxy [[HasProperty]] trap
    if (js_is_proxy(object)) {
        return js_proxy_trap_has(object, key);
    }
    // ES spec §9.4.5.2: TypedArray [[HasProperty]] — numeric indices only check bounds
    if (type == LMD_TYPE_MAP && object.map->map_kind == MAP_KIND_TYPED_ARRAY) {
        // coerce key to string for numeric check
        const char* ks = NULL; int kl = 0;
        char nbuf[64];
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* s = it2s(key);
            ks = s->chars; kl = (int)s->len;
        } else if (get_type_id(key) == LMD_TYPE_INT) {
            int64_t iv = it2i(key);
            if (iv > -(int64_t)JS_SYMBOL_BASE) { // not a symbol
                kl = snprintf(nbuf, sizeof(nbuf), "%lld", (long long)iv);
                ks = nbuf;
            }
        } else if (get_type_id(key) == LMD_TYPE_FLOAT) {
            double dv = it2d(key);
            if (dv == 0.0) { kl = 1; ks = "0"; }
            else { kl = snprintf(nbuf, sizeof(nbuf), "%g", dv); ks = nbuf; }
        }
        if (ks && kl > 0) {
            // check if this is a CanonicalNumericIndexString
            // includes: integer strings "0"-"N", "-0", floats like "1.1", "Infinity", "NaN"
            bool is_canonical_numeric = false;
            bool is_valid_index = false;
            // special cases: "-0", "Infinity", "-Infinity", "NaN" are canonical numeric but not valid indices
            if (kl == 2 && ks[0] == '-' && ks[1] == '0') {
                is_canonical_numeric = true;
                is_valid_index = false;
            } else if ((kl == 8 && strncmp(ks, "Infinity", 8) == 0) ||
                       (kl == 9 && strncmp(ks, "-Infinity", 9) == 0) ||
                       (kl == 3 && strncmp(ks, "NaN", 3) == 0)) {
                is_canonical_numeric = true;
                is_valid_index = false;
            } else {
                // try parsing as a number
                bool has_dot = false;
                bool has_digit = false;
                bool is_neg = false;
                bool all_ok = true;
                int start = 0;
                if (kl > 0 && ks[0] == '-') { is_neg = true; start = 1; }
                for (int i = start; i < kl && all_ok; i++) {
                    if (ks[i] == '.') { if (has_dot) all_ok = false; has_dot = true; }
                    else if (ks[i] >= '0' && ks[i] <= '9') has_digit = true;
                    else all_ok = false;
                }
                if (all_ok && has_digit) {
                    is_canonical_numeric = true;
                    if (!has_dot && !is_neg) {
                        // non-negative integer: valid if 0 <= idx < length and buffer not detached
                        // reject leading zeros (except "0" itself)
                        if (kl > 1 && ks[0] == '0') {
                            is_valid_index = false;
                        } else {
                            JsTypedArray* ta = js_get_typed_array_ptr(object.map);
                            if (ta && ta->buffer && ta->buffer->detached) {
                                is_valid_index = false;
                            } else {
                                int64_t idx = 0;
                                for (int i = 0; i < kl; i++) idx = idx * 10 + (ks[i] - '0');
                                is_valid_index = (ta && idx >= 0 && idx < ta->length);
                            }
                        }
                    }
                    // negative integers and floats are not valid indices
                }
            }
            if (is_canonical_numeric) {
                return (Item){.item = b2it(is_valid_index)};
            }
        }
        // non-numeric key: fall through to normal MAP handling (check own + prototype)
    }
    if (type == LMD_TYPE_MAP) {
        // Check for symbol keys FIRST (before any numeric coercion)
        // Symbol items are encoded as negative ints <= -JS_SYMBOL_BASE
        if (get_type_id(key) == LMD_TYPE_INT && it2i(key) <= -(int64_t)JS_SYMBOL_BASE) {
            int64_t id = -(it2i(key) + (int64_t)JS_SYMBOL_BASE);
            char sym_buf[32];
            snprintf(sym_buf, sizeof(sym_buf), "__sym_%lld", (long long)id);
            int sym_len = (int)strlen(sym_buf);
            // check own data property
            bool own_found = false;
            Item own_val = js_map_get_fast_ext(object.map, sym_buf, sym_len, &own_found);
            if (own_found && own_val.item != JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(true)};
            // Phase-5D: legacy __get_/__set_ symbol-key probes removed.
            // Bare-name shape entry with IS_ACCESSOR flag is detected by the
            // own data probe above (own_val is the JsAccessorPair, found=true).
            // walk prototype chain
            Item proto = js_get_prototype(object);
            int depth = 0;
            while (proto.item != ItemNull.item && get_type_id(proto) == LMD_TYPE_MAP && depth < 32) {
                if (js_is_proxy(proto)) {
                    return js_proxy_trap_has(proto, key);
                }
                bool found = false;
                Item pval = js_map_get_fast_ext(proto.map, sym_buf, sym_len, &found);
                if (found && pval.item != JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(true)};
                proto = js_get_prototype(proto);
                depth++;
            }
            return (Item){.item = b2it(false)};
        }

        // JS semantics: numeric keys are coerced to strings (17 in obj === "17" in obj)
        if (get_type_id(key) == LMD_TYPE_INT || get_type_id(key) == LMD_TYPE_FLOAT) {
            char buf[64];
            if (get_type_id(key) == LMD_TYPE_INT) {
                snprintf(buf, sizeof(buf), "%lld", (long long)it2i(key));
            } else {
                double dv = it2d(key);
                if (dv != dv) snprintf(buf, sizeof(buf), "NaN");
                else if (dv == 1.0/0.0) snprintf(buf, sizeof(buf), "Infinity");
                else if (dv == -1.0/0.0) snprintf(buf, sizeof(buf), "-Infinity");
                else if (dv == 0.0) snprintf(buf, sizeof(buf), "0");
                else snprintf(buf, sizeof(buf), "%g", dv);
            }
            key = (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
        }
        // ES spec: ToPropertyKey converts non-symbol primitives to string
        // Handle bool, null, undefined (and any other non-string type)
        if (get_type_id(key) != LMD_TYPE_STRING && !(get_type_id(key) == LMD_TYPE_INT && it2i(key) <= -(int64_t)JS_SYMBOL_BASE)) {
            key = js_to_string(key);
        }

        if (get_type_id(key) == LMD_TYPE_STRING || get_type_id(key) == LMD_TYPE_SYMBOL) {
            const char* key_str = key.get_chars();
            int key_len = (int)key.get_len();
            // 1. check own data property
            bool own_found = false;
            Item own_val = js_map_get_fast_ext(object.map, key_str, key_len, &own_found);
            if (own_found && own_val.item != JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(true)};
            // 2. Phase-5D: legacy __get_/__set_ probes removed. Bare-name shape
            //    entry with IS_ACCESSOR flag is detected by step 1 (own data probe
            //    finds the JsAccessorPair slot under the bare key).
            // 3. walk prototype chain (data properties + accessors).
            // Use js_get_prototype_of so plain MAPs without explicit __proto__
            // still walk the implicit Object.prototype chain.
            Item proto = js_get_prototype_of(object);
            int depth = 0;
            while (proto.item != ItemNull.item && get_type_id(proto) == LMD_TYPE_MAP && depth < 32) {
                // if prototype is a proxy, delegate to its [[HasProperty]] trap
                if (js_is_proxy(proto)) {
                    return js_proxy_trap_has(proto, key);
                }
                bool found = false;
                Item pval = js_map_get_fast_ext(proto.map, key_str, key_len, &found);
                if (found && pval.item != JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(true)};
                // Phase-5D: legacy __get_/__set_ proto-chain probes removed.
                // IS_ACCESSOR shape entries on protos are found by the data probe above.
                proto = js_get_prototype_of(proto);
                depth++;
            }
        } else {
            // non-string key: fall back to map_get
            Item result = map_get(object.map, key);
            if (result.item != ItemNull.item) return (Item){.item = b2it(true)};
        }
        // v26: check builtin methods on object and prototype chain
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* ks = it2s(key);
            if (ks) {
                // Check builtin methods on explicit prototype chain objects
                Item proto = js_get_prototype(object);
                int depth = 0;
                while (proto.item != ItemNull.item && get_type_id(proto) == LMD_TYPE_MAP && depth < 32) {
                    if (js_is_proxy(proto)) {
                        return js_proxy_trap_has(proto, key);
                    }
                    if (js_map_has_builtin_method(proto.map, ks->chars, (int)ks->len))
                        return (Item){.item = b2it(true)};
                    proto = js_get_prototype(proto);
                    depth++;
                }
                // Fallback: all objects implicitly inherit Object.prototype methods
                // UNLESS explicitly created with null prototype (Object.create(null))
                bool has_null_proto = false;
                bool proto_found = false;
                js_map_get_fast_ext(object.map, "__proto__", 9, &proto_found);
                if (proto_found) {
                    // __proto__ exists — walk chain to see if it terminates at null
                    Item pv = js_get_prototype(object);
                    if (pv.item == ItemNull.item || get_type_id(pv) != LMD_TYPE_MAP)
                        has_null_proto = true;
                }
                if (!has_null_proto) {
                    Item builtin = js_lookup_builtin_method(LMD_TYPE_MAP, ks->chars, (int)ks->len);
                    if (builtin.item != ItemNull.item) return (Item){.item = b2it(true)};
                    // "constructor" is also on Object.prototype
                    if (ks->len == 11 && strncmp(ks->chars, "constructor", 11) == 0)
                        return (Item){.item = b2it(true)};
                }
            }
        }
        return (Item){.item = b2it(false)};
    }
    if (type == LMD_TYPE_ARRAY) {
        // check if index is valid and not a deleted hole
        int64_t idx = -1;
        if (get_type_id(key) == LMD_TYPE_INT) {
            idx = it2i(key);
        } else if (get_type_id(key) == LMD_TYPE_STRING) {
            String* sk = it2s(key);
            if (sk && sk->len > 0 && sk->len <= 10) {
                bool is_num = true;
                int64_t n = 0;
                for (int i = 0; i < (int)sk->len; i++) {
                    char c = sk->chars[i];
                    if (c < '0' || c > '9') { is_num = false; break; }
                    n = n * 10 + (c - '0');
                }
                if (is_num && !(sk->len > 1 && sk->chars[0] == '0')) idx = n;
            }
            // Also check "length" for arrays
            if (sk && sk->len == 6 && strncmp(sk->chars, "length", 6) == 0) {
                return (Item){.item = b2it(true)};
            }
        }
        Array* arr = object.array;
        if (idx >= 0 && idx < arr->length) {
            // v25: check for deleted sentinel (array hole) — fall through to prototype
            if (arr->items[idx].item != JS_DELETED_SENTINEL_VAL) {
                return (Item){.item = b2it(true)};
            }
            // hole — fall through to prototype chain check
        }
        // Check companion map for own properties (e.g. arguments overflow)
        if (idx >= 0 && arr->extra != 0) {
            char idx_buf[32];
            snprintf(idx_buf, sizeof(idx_buf), "%lld", (long long)idx);
            Map* pm = (Map*)(uintptr_t)arr->extra;
            bool pm_found = false;
            Item pm_val = js_map_get_fast_ext(pm, idx_buf, (int)strlen(idx_buf), &pm_found);
            if (pm_found && pm_val.item != JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(true)};
        }
        // Walk prototype chain for numeric keys (inherited indexed properties)
        if (idx >= 0) {
            Item proto = js_get_prototype_of(object);
            int depth = 0;
            while (proto.item != ItemNull.item && depth < 32) {
                Item result = js_in(key, proto);
                if (result.item == ITEM_ERROR) return result;
                if (it2b(result)) return (Item){.item = b2it(true)};
                proto = js_get_prototype_of(proto);
                depth++;
            }
            return (Item){.item = b2it(false)};
        }
        // Non-numeric string key: check companion-map own properties, then walk
        // Array.prototype chain (e.g. Array.prototype.value set by user code).
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* sk = it2s(key);
            if (sk && sk->len > 0) {
                if (arr->extra != 0) {
                    Map* pm = (Map*)(uintptr_t)arr->extra;
                    bool pm_found = false;
                    Item pm_val = js_map_get_fast_ext(pm, sk->chars, (int)sk->len, &pm_found);
                    if (pm_found && pm_val.item != JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(true)};
                    // Phase 5D array migration: legacy __get_<name>/__set_<name>
                    // probes removed for named keys. Named-key accessors are stored
                    // as IS_ACCESSOR + JsAccessorPair under the bare name (found by
                    // the fast probe above). Numeric indices retain legacy markers.
                }
                Item proto = js_get_prototype_of(object);
                int depth = 0;
                while (proto.item != ItemNull.item && depth < 32) {
                    Item result = js_in(key, proto);
                    if (result.item == ITEM_ERROR) return result;
                    if (it2b(result)) return (Item){.item = b2it(true)};
                    proto = js_get_prototype_of(proto);
                    depth++;
                }
            }
        }
        return (Item){.item = b2it(false)};
    }
    if (type == LMD_TYPE_FUNC) {
        // Check function own properties (properties_map, name, length, prototype)
        if (it2b(js_has_own_property(object, key))) return (Item){.item = b2it(true)};
        // Check prototype chain and builtin methods
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* sk = it2s(key);
            if (sk) {
                // Walk prototype chain (Function.prototype → Object.prototype).
                // Use js_get_prototype_of (not js_get_prototype) so the implicit
                // Function.prototype is returned even when no custom __proto__ is set.
                Item proto = js_get_prototype_of(object);
                int depth = 0;
                while (proto.item != ItemNull.item && get_type_id(proto) == LMD_TYPE_MAP && depth < 32) {
                    bool found = false;
                    Item pval = js_map_get_fast_ext(proto.map, sk->chars, (int)sk->len, &found);
                    if (found && pval.item != JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(true)};
                    // Phase-5D: legacy __get_/__set_ proto-chain probes removed.
                    // IS_ACCESSOR shape entries on protos are detected by the data
                    // probe above (JsAccessorPair pointer != JS_DELETED_SENTINEL_VAL).
                    proto = js_get_prototype_of(proto);
                    depth++;
                }
                // Check builtin methods (call, apply, bind, toString, etc.)
                Item builtin = js_lookup_builtin_method(LMD_TYPE_FUNC, sk->chars, (int)sk->len);
                if (builtin.item != ItemNull.item) return (Item){.item = b2it(true)};
                builtin = js_lookup_builtin_method(LMD_TYPE_MAP, sk->chars, (int)sk->len);
                if (builtin.item != ItemNull.item) return (Item){.item = b2it(true)};
            }
        }
        return (Item){.item = b2it(false)};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_nullish_coalesce(Item left, Item right) {
    TypeId type = get_type_id(left);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) return right;
    return left;
}

// =============================================================================
// Object.create — create new object with specified prototype
// =============================================================================

extern "C" Item js_object_create(Item proto) {
    // Per spec: proto must be Object or null, else TypeError
    TypeId pt = get_type_id(proto);
    bool is_null = (proto.item == ITEM_NULL || proto.item == 0 || pt == LMD_TYPE_NULL);
    bool is_object = (pt == LMD_TYPE_MAP || pt == LMD_TYPE_ELEMENT || pt == LMD_TYPE_FUNC);
    if (!is_null && !is_object) {
        return js_throw_type_error("Object prototype may only be an Object or null");
    }
    Item obj = js_new_object();
    if (is_object && pt == LMD_TYPE_MAP) {
        js_set_prototype(obj, proto);
    } else if (is_null) {
        // Object.create(null): mark explicitly as no prototype
        // Use JS undefined as sentinel — distinguished from "no __proto__ key"
        Item key = (Item){.item = s2it(heap_create_name("__proto__", 9))};
        js_property_set(obj, key, make_js_undefined());
    }
    return obj;
}

// =============================================================================
// Object.getPrototypeOf — returns enriched prototype with methods/getters
// =============================================================================
// In standard JS, class methods and getters live on the prototype object.
// In our engine, they live on each instance. To support $clone() patterns like
// Object.create(Object.getPrototypeOf(this)), we create a rich prototype that
// includes __class_name__, __get_*, __set_*, and function-valued entries from
// the source instance, chained to the original __proto__ for instanceof support.

// Forward declarations for %TypedArray% intrinsic (defined later in file)
extern "C" bool js_is_typed_array_ctor_name(const char* name, int len);
extern "C" Item js_get_typed_array_base();
extern "C" Item js_func_get_custom_proto(Item func);
extern "C" Item js_array_get_custom_proto(Item arr);

// v90: GeneratorFunction.prototype singleton — returned by Object.getPrototypeOf for generator functions.
// Its .constructor creates generator-flagged functions (non-constructable via 'new').
static Item js_generator_function_proto_cache = {0};
static Item js_async_generator_function_proto_cache = {0};
// AsyncFunction.prototype singleton — returned by Object.getPrototypeOf for async (non-generator) functions.
static Item js_async_function_proto_cache = {0};

extern "C" Item js_new_function(void* func_ptr, int param_count);
extern "C" void js_mark_generator_func(Item fn_item);

static Item js_gen_func_ctor_placeholder(Item* args, int argc) {
    (void)args; (void)argc;
    return ItemNull;
}

// Separate placeholder for async generator function constructor.
// Must be a DIFFERENT function pointer so js_new_function returns a separate
// cached JsFunction* for async vs sync generator constructors.
static Item js_async_gen_func_ctor_placeholder(Item* args, int argc) {
    (void)args; (void)argc;
    return ItemNull;
}

// AsyncFunction constructor placeholder (distinct func_ptr for caching).
static Item js_async_func_ctor_placeholder(Item* args, int argc) {
    (void)args; (void)argc;
    return ItemNull;
}

// Partial layout to access flags field (full JsFunctionLayout defined below)
struct JsFuncFlagsAccess {
    TypeId type_id;
    void* func_ptr;
    int param_count;
    Item* env;
    int env_size;
    Item prototype;
    Item bound_this;
    Item* bound_args;
    int bound_argc;
    String* name;
    int builtin_id;
    Item properties_map;
    uint8_t flags;
};
#define JS_FUNC_FLAG_GENERATOR_EARLY 1

static Item js_get_generator_function_prototype(bool is_async) {
    Item* cache = is_async ? &js_async_generator_function_proto_cache : &js_generator_function_proto_cache;
    if (cache->item != 0) return *cache;

    // Create a MAP to serve as GeneratorFunction.prototype (or AsyncGeneratorFunction.prototype)
    Item proto = js_object_create(ItemNull);
    if (get_type_id(proto) != LMD_TYPE_MAP) return ItemNull;

    // Create the constructor function — use DIFFERENT func_ptr for sync vs async so that
    // js_new_function() caches them separately. Sharing the same func_ptr would cause
    // the second call to overwrite ctor_fn->prototype of the first.
    const char* ctor_name = is_async ? "AsyncGeneratorFunction" : "GeneratorFunction";
    void* ctor_fptr = is_async ? (void*)js_async_gen_func_ctor_placeholder : (void*)js_gen_func_ctor_placeholder;
    Item ctor_fn = js_new_function(ctor_fptr, 1);
    if (get_type_id(ctor_fn) == LMD_TYPE_FUNC) {
        JsFuncFlagsAccess* fn = (JsFuncFlagsAccess*)ctor_fn.function;
        fn->name = heap_create_name(ctor_name, strlen(ctor_name));
    }

    // Set .constructor on the prototype
    Item ctor_key = (Item){.item = s2it(heap_create_name("constructor", 11))};
    js_property_set(proto, ctor_key, ctor_fn);

    // v90: Set the constructor's .prototype field to the proto MAP so
    // Object.getOwnPropertyDescriptor returns the correct value.
    if (get_type_id(ctor_fn) == LMD_TYPE_FUNC) {
        JsFuncFlagsAccess* cfn = (JsFuncFlagsAccess*)ctor_fn.function;
        cfn->prototype = proto;
    }

    // Per ES spec §27.6.3.1 / §27.3.3.1:
    // GeneratorFunction.prototype.prototype === %AsyncGeneratorPrototype% / %GeneratorPrototype%
    // This means: Object.getPrototypeOf(genFunc).prototype === depth-2
    {
        Item depth2 = js_get_generator_shared_proto(is_async);
        Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
        js_property_set(proto, proto_key, depth2);
        js_mark_non_writable(proto, proto_key);
        js_mark_non_enumerable(proto, proto_key);
    }

    *cache = proto;
    return proto;
}

// AsyncFunction.prototype singleton — analog of generator-function prototype but for
// non-generator async functions. Object.getPrototypeOf(asyncFn) === this.
static Item js_get_async_function_prototype() {
    if (js_async_function_proto_cache.item != 0) return js_async_function_proto_cache;
    Item proto = js_object_create(ItemNull);
    if (get_type_id(proto) != LMD_TYPE_MAP) return ItemNull;
    Item ctor_fn = js_new_function((void*)js_async_func_ctor_placeholder, 1);
    if (get_type_id(ctor_fn) == LMD_TYPE_FUNC) {
        JsFuncFlagsAccess* fn = (JsFuncFlagsAccess*)ctor_fn.function;
        fn->name = heap_create_name("AsyncFunction", 13);
    }
    Item ctor_key = (Item){.item = s2it(heap_create_name("constructor", 11))};
    js_property_set(proto, ctor_key, ctor_fn);
    if (get_type_id(ctor_fn) == LMD_TYPE_FUNC) {
        JsFuncFlagsAccess* cfn = (JsFuncFlagsAccess*)ctor_fn.function;
        cfn->prototype = proto;
    }
    js_async_function_proto_cache = proto;
    return proto;
}

extern "C" Item js_get_prototype_of(Item object) {
    // Proxy [[GetPrototypeOf]] trap
    if (js_is_proxy(object)) {
        return js_proxy_trap_get_prototype_of(object);
    }
    // ES6: ToObject for primitives
    TypeId ot = get_type_id(object);
    if (ot == LMD_TYPE_STRING) {
        Item ctor = js_get_constructor((Item){.item = s2it(heap_create_name("String", 6))});
        return js_property_get(ctor, (Item){.item = s2it(heap_create_name("prototype", 9))});
    }
    if (ot == LMD_TYPE_INT || ot == LMD_TYPE_FLOAT) {
        Item ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Number", 6))});
        return js_property_get(ctor, (Item){.item = s2it(heap_create_name("prototype", 9))});
    }
    if (ot == LMD_TYPE_BOOL) {
        Item ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Boolean", 7))});
        return js_property_get(ctor, (Item){.item = s2it(heap_create_name("prototype", 9))});
    }
    if (!js_require_object_type(object, "getPrototypeOf")) return ItemNull;
    // v18g: Arrays → return Array.prototype (or custom if set via Object.setPrototypeOf)
    if (get_type_id(object) == LMD_TYPE_ARRAY) {
        Item custom_proto = js_array_get_custom_proto(object);
        if (custom_proto.item != ItemNull.item) return custom_proto;
        Item arr_ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Array", 5))});
        if (get_type_id(arr_ctor) == LMD_TYPE_FUNC) {
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            return js_property_get(arr_ctor, proto_key);
        }
        return ItemNull;
    }
    // Functions → return Function.prototype (or Error for NativeError constructors)
    if (get_type_id(object) == LMD_TYPE_FUNC) {
        // Check for custom __proto__ set via Object.setPrototypeOf
        Item custom_proto = js_func_get_custom_proto(object);
        if (custom_proto.item != ItemNull.item) return custom_proto;
        // v82d: NativeError constructors have [[Prototype]] = Error (not Function.prototype)
        // Check .name property to see if it's a NativeError constructor
        Item name_key = (Item){.item = s2it(heap_create_name("name", 4))};
        Item name_val = js_property_get(object, name_key);
        if (get_type_id(name_val) == LMD_TYPE_STRING) {
            String* n = it2s(name_val);
            bool is_native_error =
                (n->len == 9 && strncmp(n->chars, "TypeError", 9) == 0) ||
                (n->len == 10 && strncmp(n->chars, "RangeError", 10) == 0) ||
                (n->len == 14 && strncmp(n->chars, "ReferenceError", 14) == 0) ||
                (n->len == 11 && strncmp(n->chars, "SyntaxError", 11) == 0) ||
                (n->len == 8 && strncmp(n->chars, "URIError", 8) == 0) ||
                (n->len == 9 && strncmp(n->chars, "EvalError", 9) == 0) ||
                (n->len == 14 && strncmp(n->chars, "AggregateError", 14) == 0);
            if (is_native_error) {
                return js_get_constructor((Item){.item = s2it(heap_create_name("Error", 5))});
            }
            // TypedArray constructors → [[Prototype]] = %TypedArray%
            if (js_is_typed_array_ctor_name(n->chars, (int)n->len)) {
                return js_get_typed_array_base();
            }
        }
        // v90: Generator functions → return GeneratorFunction.prototype
        {
            JsFuncFlagsAccess* fn = (JsFuncFlagsAccess*)object.function;
            if (fn->flags & JS_FUNC_FLAG_GENERATOR_EARLY) {
                // Check if it's an async generator (ASYNC_GEN flag = 64)
                bool is_async_gen = (fn->flags & 64) != 0;
                return js_get_generator_function_prototype(is_async_gen);
            }
            // Async (non-generator) functions → AsyncFunction.prototype (flag 128)
            if (fn->flags & 128) {
                return js_get_async_function_prototype();
            }
        }
        Item func_ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Function", 8))});
        if (get_type_id(func_ctor) == LMD_TYPE_FUNC) {
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            return js_property_get(func_ctor, proto_key);
        }
        return ItemNull;
    }
    if (get_type_id(object) != LMD_TYPE_MAP) return ItemNull;

    // TypedArray instances → check custom __proto__ first, then %TypedArray%.prototype
    if (object.map->map_kind == MAP_KIND_TYPED_ARRAY) {
        Item custom = js_get_prototype(object);
        if (custom.item != ItemNull.item && custom.item != ITEM_JS_UNDEFINED) return custom;
        extern Item js_get_typed_array_base_proto();
        return js_get_typed_array_base_proto();
    }

    // v18h: Check if this is a class object (has __instance_proto__) → return Function.prototype
    {
        bool own_ip = false;
        js_map_get_fast_ext(object.map, "__instance_proto__", 18, &own_ip);
        if (own_ip) {
            // Class objects inherit from Function.prototype
            // Check for __proto__ first (set by extends)
            Item raw = js_get_prototype(object);
            if (raw.item != ItemNull.item) return raw;
            Item func_ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Function", 8))});
            if (get_type_id(func_ctor) == LMD_TYPE_FUNC) {
                Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
                return js_property_get(func_ctor, proto_key);
            }
            return ItemNull;
        }
    }

    // v18l: Check __proto__ first — Object.create sets this explicitly
    {
        Item raw_proto = js_get_prototype(object);
        // Object.create(null) stores undefined as sentinel for null prototype
        if (raw_proto.item == ITEM_JS_UNDEFINED) return ItemNull;
        if (raw_proto.item != ItemNull.item) return raw_proto;
    }

    // v18g: If instance has a constructor with a .prototype property,
    // AND the object is NOT that prototype itself, return constructor.prototype
    {
        Item ctor_key = (Item){.item = s2it(heap_create_name("constructor", 11))};
        Item ctor = js_property_get(object, ctor_key);
        if (get_type_id(ctor) == LMD_TYPE_MAP) {
            // user-defined class: read its "prototype" property
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            Item proto = js_property_get(ctor, proto_key);
            // Guard: don't return self (prototype objects have constructor.prototype === self)
            if (get_type_id(proto) == LMD_TYPE_MAP && proto.map != object.map) return proto;
        } else if (get_type_id(ctor) == LMD_TYPE_FUNC) {
            // built-in constructor: get .prototype via property_get (triggers lazy init)
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            Item proto = js_property_get(ctor, proto_key);
            if (get_type_id(proto) == LMD_TYPE_MAP && proto.map != object.map) {
                return proto;
            }
        }
    }

    // No __proto__ found — return Object.prototype for plain objects
    Item obj_ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Object", 6))});
    if (get_type_id(obj_ctor) == LMD_TYPE_FUNC) {
        Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
        Item obj_proto = js_property_get(obj_ctor, proto_key);
        // if object IS Object.prototype itself, return null (end of chain)
        if (get_type_id(obj_proto) == LMD_TYPE_MAP && obj_proto.map == object.map) {
            return ItemNull;
        }
        return obj_proto;
    }
    return ItemNull;
}

// =============================================================================
// Reflect.construct(target, argumentsList[, newTarget])
// Equivalent to: new target(...argumentsList)
// =============================================================================

extern Item js_constructor_create_object(Item callee);
extern Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
extern Item js_array_get(Item array, Item index);
extern int64_t js_array_length(Item array);
extern void js_throw_value(Item value);
extern Item js_new_error_with_name(Item type_name, Item msg);
extern Item js_array_new(int length);
extern Item js_array_new_from_item(Item arg);
// js_array_push already declared above as extern "C" Item js_array_push(Item, Item)
extern Item js_date_new();
extern Item js_date_new_from(Item arg);
extern Item js_date_new_multi(Item args_arr);
extern Item js_regexp_construct(Item pattern, Item flags);
extern Item js_map_collection_new();
extern Item js_map_collection_new_from(Item iterable);
extern Item js_set_collection_new();
extern Item js_set_collection_new_from(Item iterable);
extern Item js_weakmap_new();
extern Item js_weakset_new();
extern Item js_promise_create(Item executor);
extern Item js_arraybuffer_construct(Item length);
extern Item js_dataview_new(Item buffer, Item offset, Item length);
extern Item js_typed_array_construct(int type, Item arg, int offset, int len, int argc);
extern Item js_throw_type_error(const char* msg);

// Check if a function value is a constructor (has [[Construct]] internal method).
// Arrow functions, generators, and built-in prototype methods are NOT constructors.
#define JS_FUNC_FLAG_GENERATOR_G 1
#define JS_FUNC_FLAG_ARROW_G     2
#define JS_FUNC_FLAG_METHOD_G    32

struct JsFunctionLayout {
    TypeId type_id;
    void* func_ptr;
    int param_count;
    Item* env;
    int env_size;
    Item prototype;
    Item bound_this;
    Item* bound_args;
    int bound_argc;
    String* name;
    int builtin_id;
    Item properties_map;
    uint8_t flags;
    int16_t formal_length;
};

static bool js_func_is_constructor(Item func_item) {
    // Proxy: a proxy is constructable only if its target is constructable
    if (js_is_proxy(func_item)) {
        Item target = js_proxy_get_target(func_item);
        return js_func_is_constructor(target);
    }
    if (get_type_id(func_item) != LMD_TYPE_FUNC) return false;
    JsFunctionLayout* fn = (JsFunctionLayout*)func_item.function;
    if (fn->flags & JS_FUNC_FLAG_ARROW_G) return false;
    if (fn->flags & JS_FUNC_FLAG_GENERATOR_G) return false;
    if (fn->flags & JS_FUNC_FLAG_METHOD_G) return false;
    if (fn->builtin_id > 0) return false;
    if (fn->builtin_id == -2) return false; // global builtin wrappers are not constructors
    return true;
}

// Check if a function has its own .prototype property.
// Constructors and generators have .prototype; arrows and built-ins do not.
static bool js_func_has_own_prototype(Item func_item) {
    if (get_type_id(func_item) != LMD_TYPE_FUNC) return false;
    JsFunctionLayout* fn = (JsFunctionLayout*)func_item.function;
    if (fn->flags & (JS_FUNC_FLAG_ARROW_G | JS_FUNC_FLAG_METHOD_G)) return false;
    if (fn->builtin_id > 0) return false;
    if (fn->builtin_id == -2) return false;
    return true;
}

extern "C" Item js_reflect_construct(Item target, Item args_array, Item new_target) {
    // Validate target is a constructor
    if (!js_func_is_constructor(target)) {
        Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item msg = (Item){.item = s2it(heap_create_name("target is not a constructor"))};
        Item error = js_new_error_with_name(type_name, msg);
        js_throw_value(error);
        return ItemNull;
    }
    // Validate newTarget is a constructor (if provided and not undefined/null)
    TypeId nt_type = get_type_id(new_target);
    if (nt_type == LMD_TYPE_FUNC || js_is_proxy(new_target)) {
        if (!js_func_is_constructor(new_target)) {
            Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
            Item msg = (Item){.item = s2it(heap_create_name("newTarget is not a constructor"))};
            Item error = js_new_error_with_name(type_name, msg);
            js_throw_value(error);
            return ItemNull;
        }
    }
    // extract args from array
    int argc = 0;
    Item* args = NULL;
    if (get_type_id(args_array) == LMD_TYPE_ARRAY) {
        argc = (int)js_array_length(args_array);
        if (argc > 0) {
            args = (Item*)alloca(argc * sizeof(Item));
            for (int i = 0; i < argc; i++) {
                Item idx = {.item = i2it(i)};
                args[i] = js_array_get(args_array, idx);
            }
        }
    }
    // For proxy targets, delegate to [[Construct]] trap
    if (js_is_proxy(target)) {
        return js_proxy_trap_construct(target, args, argc, 
            (nt_type == LMD_TYPE_FUNC || js_is_proxy(new_target)) ? new_target : target);
    }
    // ES spec: built-in constructors validate arguments BEFORE OrdinaryCreateFromConstructor
    // (which accesses NewTarget.prototype). Perform those checks here to ensure the correct
    // error ordering when NewTarget has a throwing .prototype getter.
    if (get_type_id(target) == LMD_TYPE_FUNC) {
        JsFunctionLayout* fn = (JsFunctionLayout*)target.function;
        if (fn->name) {
            const char* n = fn->name->chars;
            int nl = (int)fn->name->len;
            // Promise(executor): IsCallable(executor) check before prototype access
            if (nl == 7 && strncmp(n, "Promise", 7) == 0) {
                Item executor = (argc > 0) ? args[0] : ItemNull;
                if (get_type_id(executor) != LMD_TYPE_FUNC) {
                    extern Item js_throw_type_error(const char* msg);
                    return js_throw_type_error("Promise resolver is not a function");
                }
            }
            // TypedArray(arg): ToNumber(arg) throws for Symbol/BigInt before prototype access
            if (argc > 0) {
                bool is_ta = (nl == 9  && strncmp(n, "Int8Array", 9) == 0) ||
                             (nl == 10 && strncmp(n, "Uint8Array", 10) == 0) ||
                             (nl == 17 && strncmp(n, "Uint8ClampedArray", 17) == 0) ||
                             (nl == 10 && strncmp(n, "Int16Array", 10) == 0) ||
                             (nl == 11 && strncmp(n, "Uint16Array", 11) == 0) ||
                             (nl == 10 && strncmp(n, "Int32Array", 10) == 0) ||
                             (nl == 11 && strncmp(n, "Uint32Array", 11) == 0) ||
                             (nl == 12 && strncmp(n, "Float32Array", 12) == 0) ||
                             (nl == 12 && strncmp(n, "Float64Array", 12) == 0);
                if (is_ta) {
                    // JS symbols are encoded as negative ints (LMD_TYPE_INT with value <= -JS_SYMBOL_BASE)
                    TypeId at = get_type_id(args[0]);
                    bool is_sym = (at == LMD_TYPE_SYMBOL) || 
                                  (at == LMD_TYPE_INT && it2i(args[0]) <= -(int64_t)(1LL << 40));
                    if (is_sym) {
                        extern Item js_throw_type_error(const char* msg);
                        return js_throw_type_error("Cannot convert a Symbol value to a number");
                    }
                }
            }
            // DataView: spec steps 3 (ToIndex byteOffset) and 6 (offset > bufferLength check) happen
            // BEFORE step 10 (OrdinaryCreateFromConstructor which accesses NewTarget.prototype).
            // Perform the bounds check here so the RangeError fires before the prototype getter.
            if (nl == 8 && strncmp(n, "DataView", 8) == 0) {
                Item buf_arg = (argc > 0) ? args[0] : ItemNull;
                if (js_is_arraybuffer(buf_arg)) {
                    int ab_len = js_arraybuffer_byte_length(buf_arg);
                    Item off_arg = (argc > 1) ? args[1] : (Item){.item = ITEM_JS_UNDEFINED};
                    TypeId ot2 = get_type_id(off_arg);
                    int dv_off = 0;
                    if (ot2 != LMD_TYPE_UNDEFINED && ot2 != LMD_TYPE_NULL) {
                        if (ot2 == LMD_TYPE_INT) dv_off = (int)it2i(off_arg);
                        else if (ot2 == LMD_TYPE_FLOAT) { double dv_d = it2d(off_arg); dv_off = (dv_d != dv_d) ? 0 : (int)dv_d; }
                        // For objects: ToIndex will be called in js_dataview_new; skip pre-check here
                        else { dv_off = -1; } // sentinel: don't pre-check (let constructor handle it)
                    }
                    if (dv_off >= 0 && dv_off > ab_len) {
                        return js_throw_range_error("Start offset is outside the bounds of the buffer");
                    }
                }
            }
        }
    }
    extern void js_set_new_target(Item target);
    Item nt_val = (nt_type == LMD_TYPE_FUNC || js_is_proxy(new_target)) ? new_target : target;
    js_set_new_target(nt_val);

    // ES spec: OrdinaryCreateFromConstructor accesses NewTarget.prototype BEFORE
    // the built-in constructor does any work. Eagerly resolve the prototype now
    // so that a throwing getter on .prototype fires at the correct time.
    Item resolved_nt_proto = ItemNull;
    bool needs_fixup = (nt_type == LMD_TYPE_FUNC && new_target.item != target.item);
    if (needs_fixup) {
        Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
        resolved_nt_proto = js_property_get(new_target, proto_key);
    }

    // Helper: apply the pre-resolved prototype to a newly constructed built-in object
    auto fixup_proto = [&](Item result) -> Item {
        if (needs_fixup && get_type_id(resolved_nt_proto) == LMD_TYPE_MAP) {
            Item proto_set_key = (Item){.item = s2it(heap_create_name("__proto__", 9))};
            js_property_set(result, proto_set_key, resolved_nt_proto);
        }
        return result;
    };

    // For built-in constructors, dispatch by name (since their func_ptr only takes 1-2 args)
    if (get_type_id(target) == LMD_TYPE_FUNC) {
        JsFunctionLayout* fn = (JsFunctionLayout*)target.function;
        if (fn->name) {
            const char* n = fn->name->chars;
            int nl = (int)fn->name->len;

            // Array
            if (nl == 5 && strncmp(n, "Array", 5) == 0) {
                if (argc == 0) return fixup_proto(js_array_new(0));
                if (argc == 1) return fixup_proto(js_array_new_from_item(args[0]));
                Item arr = js_array_new(0);
                for (int i = 0; i < argc; i++) js_array_push(arr, args[i]);
                return fixup_proto(arr);
            }

            // Number, String, Boolean — fall through to generic constructor path

            // Date
            if (nl == 4 && strncmp(n, "Date", 4) == 0) {
                if (argc == 0) return fixup_proto(js_date_new());
                if (argc == 1) return fixup_proto(js_date_new_from(args[0]));
                Item arr = js_array_new(argc);
                for (int i = 0; i < argc; i++) js_array_push(arr, args[i]);
                return fixup_proto(js_date_new_multi(arr));
            }

            // RegExp
            if (nl == 6 && strncmp(n, "RegExp", 6) == 0) {
                Item pattern = (argc > 0) ? args[0] : (Item){.item = s2it(heap_create_name("", 0))};
                Item flags = (argc > 1) ? args[1] : (Item){.item = s2it(heap_create_name("", 0))};
                return fixup_proto(js_regexp_construct(pattern, flags));
            }

            // Error and subclasses
            if ((nl == 5 && strncmp(n, "Error", 5) == 0) ||
                (nl == 9 && strncmp(n, "TypeError", 9) == 0) ||
                (nl == 10 && strncmp(n, "RangeError", 10) == 0) ||
                (nl == 14 && strncmp(n, "ReferenceError", 14) == 0) ||
                (nl == 11 && strncmp(n, "SyntaxError", 11) == 0) ||
                (nl == 8 && strncmp(n, "URIError", 8) == 0) ||
                (nl == 9 && strncmp(n, "EvalError", 9) == 0)) {
                Item tn = (Item){.item = s2it(heap_create_name(n, nl))};
                Item msg = (argc > 0) ? args[0] : make_js_undefined();
                return fixup_proto(js_new_error_with_name(tn, msg));
            }

            // Map
            if (nl == 3 && strncmp(n, "Map", 3) == 0) {
                if (argc > 0) return fixup_proto(js_map_collection_new_from(args[0]));
                return fixup_proto(js_map_collection_new());
            }

            // Set
            if (nl == 3 && strncmp(n, "Set", 3) == 0) {
                if (argc > 0) return fixup_proto(js_set_collection_new_from(args[0]));
                return fixup_proto(js_set_collection_new());
            }

            // WeakMap
            if (nl == 7 && strncmp(n, "WeakMap", 7) == 0) {
                return fixup_proto(js_weakmap_new());
            }

            // WeakSet
            if (nl == 7 && strncmp(n, "WeakSet", 7) == 0) {
                return fixup_proto(js_weakset_new());
            }

            // Promise
            if (nl == 7 && strncmp(n, "Promise", 7) == 0) {
                Item executor = (argc > 0) ? args[0] : ItemNull;
                return fixup_proto(js_promise_create(executor));
            }

            // ArrayBuffer
            if (nl == 11 && strncmp(n, "ArrayBuffer", 11) == 0) {
                Item blen = (argc > 0) ? args[0] : ItemNull;
                return fixup_proto(js_arraybuffer_construct(blen));
            }

            // DataView
            if (nl == 8 && strncmp(n, "DataView", 8) == 0) {
                Item buf = (argc > 0) ? args[0] : ItemNull;
                Item off = (argc > 1) ? args[1] : (Item){.item = ITEM_JS_UNDEFINED};
                Item dvlen = (argc > 2) ? args[2] : (Item){.item = ITEM_JS_UNDEFINED};
                return fixup_proto(js_dataview_new(buf, off, dvlen));
            }

            // TypedArrays
            {
                int ta_type = -1;
                if      (nl == 9  && strncmp(n, "Int8Array", 9) == 0)           ta_type = 0;
                else if (nl == 10 && strncmp(n, "Uint8Array", 10) == 0)         ta_type = 1;
                else if (nl == 17 && strncmp(n, "Uint8ClampedArray", 17) == 0)  ta_type = 2;
                else if (nl == 10 && strncmp(n, "Int16Array", 10) == 0)         ta_type = 3;
                else if (nl == 11 && strncmp(n, "Uint16Array", 11) == 0)        ta_type = 4;
                else if (nl == 10 && strncmp(n, "Int32Array", 10) == 0)         ta_type = 5;
                else if (nl == 11 && strncmp(n, "Uint32Array", 11) == 0)        ta_type = 6;
                else if (nl == 12 && strncmp(n, "Float32Array", 12) == 0)       ta_type = 7;
                else if (nl == 12 && strncmp(n, "Float64Array", 12) == 0)       ta_type = 8;
                if (ta_type >= 0) {
                    Item arg = (argc > 0) ? args[0] : ItemNull;
                    int off = (argc > 1) ? (int)it2i(args[1]) : 0;
                    int tlen = (argc > 2) ? (int)it2i(args[2]) : -1;
                    return fixup_proto(js_typed_array_construct(ta_type, arg, off, tlen, argc));
                }
            }

            // Symbol — not constructable
            if (nl == 6 && strncmp(n, "Symbol", 6) == 0) {
                return js_throw_type_error("Symbol is not a constructor");
            }

            // Object
            if (nl == 6 && strncmp(n, "Object", 6) == 0) {
                if (argc > 0) return fixup_proto(js_to_object(args[0]));
                return fixup_proto(js_new_object());
            }
        }
    }

    // User-defined constructor: create object and call function
    Item proto_source = (nt_type == LMD_TYPE_FUNC) ? new_target : target;
    Item new_obj = js_constructor_create_object(proto_source);
    Item result = js_call_function(target, new_obj, args, argc);
    // ES spec: any non-primitive return value takes precedence
    TypeId rt = get_type_id(result);
    if (rt == LMD_TYPE_MAP || rt == LMD_TYPE_ARRAY || rt == LMD_TYPE_ELEMENT ||
        rt == LMD_TYPE_FUNC || rt == LMD_TYPE_OBJECT) {
        return result;
    }
    return new_obj;
}

// Forward declaration; defined later in this file.
static int64_t js_parse_array_index(const char* s, int len);

// Comparator used by qsort in js_reflect_own_keys to order integer indices ASC.
static int js_idx_pair_cmp(const void* a, const void* b) {
    int64_t ia = ((const int64_t*)a)[0];
    int64_t ib = ((const int64_t*)b)[0];
    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
}

// Reflect.ownKeys(obj) — returns array of all own property keys (strings + symbols)
extern "C" Item js_reflect_own_keys(Item obj) {
    // ES §28.1.13 Reflect.ownKeys: target must be an Object.
    if (!js_require_object_type(obj, "ownKeys")) return ItemNull;
    // Proxy [[OwnKeys]] trap
    if (js_is_proxy(obj)) {
        return js_proxy_trap_own_keys(obj);
    }
    // get string keys via getOwnPropertyNames
    Item names = js_object_get_own_property_names(obj);
    // get symbol keys via getOwnPropertySymbols
    Item symbols = js_object_get_own_property_symbols(obj);
    // Reorder per ES §10.1.11.1 OrdinaryOwnPropertyKeys:
    //   1) integer indices in ascending numeric order
    //   2) other string keys in insertion order
    //   3) symbols in insertion order
    Item result = js_array_new(0);
    if (get_type_id(names) == LMD_TYPE_ARRAY) {
        int n = (int)js_array_length(names);
        int64_t* idx_pairs = n > 0 ? (int64_t*)malloc(sizeof(int64_t) * 2 * n) : NULL;
        int idx_count = 0;
        for (int i = 0; i < n; i++) {
            Item k = js_array_get(names, (Item){.item = i2it(i)});
            if (get_type_id(k) == LMD_TYPE_STRING) {
                String* ks = it2s(k);
                int64_t parsed = js_parse_array_index(ks->chars, (int)ks->len);
                if (parsed >= 0) {
                    idx_pairs[idx_count * 2 + 0] = parsed;
                    idx_pairs[idx_count * 2 + 1] = (int64_t)k.item;
                    idx_count++;
                }
            }
        }
        if (idx_count > 1) qsort(idx_pairs, idx_count, sizeof(int64_t) * 2, js_idx_pair_cmp);
        for (int i = 0; i < idx_count; i++) {
            Item k = (Item){.item = (uint64_t)idx_pairs[i * 2 + 1]};
            js_array_push(result, k);
        }
        // Then string keys (skipping integer indices) in insertion order.
        for (int i = 0; i < n; i++) {
            Item k = js_array_get(names, (Item){.item = i2it(i)});
            if (get_type_id(k) == LMD_TYPE_STRING) {
                String* ks = it2s(k);
                if (js_parse_array_index(ks->chars, (int)ks->len) >= 0) continue;
            }
            js_array_push(result, k);
        }
        if (idx_pairs) free(idx_pairs);
    }
    // Symbols last, in insertion order.
    if (get_type_id(symbols) == LMD_TYPE_ARRAY) {
        int sym_len = (int)js_array_length(symbols);
        for (int i = 0; i < sym_len; i++) {
            Item sym = js_array_get(symbols, (Item){.item = i2it(i)});
            js_array_push(result, sym);
        }
    }
    return result;
}

// Reflect.set(target, key, value [, receiver]) — returns boolean.
// ES §28.1.14 → §10.1.9.1 OrdinarySet → §10.1.9.2 OrdinarySetWithOwnDescriptor.
extern "C" Item js_reflect_set(Item target, Item key, Item value, Item receiver) {
    if (!js_require_object_type(target, "set")) return ItemNull;
    // 3-arg call sites (old transpiler path) pass ItemNull; treat as receiver = target.
    if (receiver.item == ItemNull.item) receiver = target;

    // Proxy/TypedArray fast paths BEFORE ToPropertyKey: integer index dispatch
    // in js_property_set requires the original int key, not stringified.
    if (js_is_proxy(target)) {
        Item r = js_property_set(target, key, value);
        if (r.item == (uint64_t)b2it(false)) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(true)};
    }
    if (get_type_id(target) == LMD_TYPE_MAP &&
        target.map && target.map->map_kind == MAP_KIND_TYPED_ARRAY) {
        js_property_set(target, key, value);
        return (Item){.item = b2it(true)};
    }
    key = js_to_property_key(key);
    if (js_check_exception()) return ItemNull;
    // If receiver != target, fall back to OrdinarySetWithOwnDescriptor below.
    // If receiver == target and target is plain Array/Map without indexed
    // accessor traps, the legacy fast path is correct and preserves prior
    // behavior (avoids subtle regressions in shape-keyed array writes).
    if (receiver.item == target.item) {
        js_property_set(target, key, value);
        return (Item){.item = b2it(true)};
    }

    // Walk prototype chain to find the descriptor that governs this Set.
    Item ownDesc = ItemNull;
    Item cur = target;
    int depth = 0;
    while (cur.item != ItemNull.item && depth < 100) {
        ownDesc = js_object_get_own_property_descriptor(cur, key);
        if (get_type_id(ownDesc) == LMD_TYPE_MAP) break;
        cur = js_get_prototype_of(cur);
        depth++;
    }

    bool desc_present = (get_type_id(ownDesc) == LMD_TYPE_MAP);
    Item set_key = (Item){.item = s2it(heap_create_name("set", 3))};
    Item get_key = (Item){.item = s2it(heap_create_name("get", 3))};
    Item value_key = (Item){.item = s2it(heap_create_name("value", 5))};
    Item writable_key = (Item){.item = s2it(heap_create_name("writable", 8))};

    if (desc_present) {
        // Accessor descriptor? has `set` or `get` field on the descriptor object.
        bool has_set = false, has_get = false;
        Item set_fn = ItemNull, get_fn = ItemNull;
        // Use shape lookup directly — js_property_get may walk prototype.
        if (get_type_id(ownDesc) == LMD_TYPE_MAP) {
            String* sks = it2s(set_key);
            Item sv = js_map_get_fast_ext(ownDesc.map, sks->chars, (int)sks->len, &has_set);
            if (has_set) set_fn = sv;
            String* gks = it2s(get_key);
            Item gv = js_map_get_fast_ext(ownDesc.map, gks->chars, (int)gks->len, &has_get);
            if (has_get) get_fn = gv;
        }
        if (has_set || has_get) {
            // Accessor descriptor.
            if (!has_set || get_type_id(set_fn) != LMD_TYPE_FUNC) {
                return (Item){.item = b2it(false)};
            }
            Item args[1] = { value };
            js_call_function(set_fn, receiver, args, 1);
            if (js_check_exception()) return ItemNull;
            return (Item){.item = b2it(true)};
        }
        // Data descriptor: check writable.
        bool has_w = false;
        Item wv = js_map_get_fast_ext(ownDesc.map,
            it2s(writable_key)->chars, (int)it2s(writable_key)->len, &has_w);
        bool writable = has_w ? it2b(js_to_boolean(wv)) : true;
        if (!writable) return (Item){.item = b2it(false)};
        // Receiver must be an Object.
        TypeId rt = get_type_id(receiver);
        bool recv_is_obj = (rt == LMD_TYPE_MAP || rt == LMD_TYPE_ARRAY ||
                            rt == LMD_TYPE_FUNC || rt == LMD_TYPE_ELEMENT);
        if (!recv_is_obj) return (Item){.item = b2it(false)};
        // If receiver != target, write to receiver per OrdinarySetWithOwnDescriptor.
        if (receiver.item != target.item) {
            // Existing own descriptor on receiver?
            Item recv_own = js_object_get_own_property_descriptor(receiver, key);
            if (get_type_id(recv_own) == LMD_TYPE_MAP) {
                bool r_has_set = false, r_has_get = false;
                js_map_get_fast_ext(recv_own.map,
                    it2s(set_key)->chars, (int)it2s(set_key)->len, &r_has_set);
                js_map_get_fast_ext(recv_own.map,
                    it2s(get_key)->chars, (int)it2s(get_key)->len, &r_has_get);
                if (r_has_set || r_has_get) {
                    // Accessor on receiver: cannot replace via Set.
                    return (Item){.item = b2it(false)};
                }
                bool rh_w = false;
                Item rw = js_map_get_fast_ext(recv_own.map,
                    it2s(writable_key)->chars, (int)it2s(writable_key)->len, &rh_w);
                bool r_writable = rh_w ? it2b(js_to_boolean(rw)) : true;
                if (!r_writable) return (Item){.item = b2it(false)};
            }
            // CreateDataProperty(receiver, key, value).
            js_property_set(receiver, key, value);
            return (Item){.item = b2it(true)};
        }
        // receiver == target: ordinary write.
        js_property_set(target, key, value);
        return (Item){.item = b2it(true)};
    }
    // No descriptor anywhere on chain → CreateDataProperty(receiver, key, value).
    {
        TypeId rt = get_type_id(receiver);
        bool recv_is_obj = (rt == LMD_TYPE_MAP || rt == LMD_TYPE_ARRAY ||
                            rt == LMD_TYPE_FUNC || rt == LMD_TYPE_ELEMENT);
        if (!recv_is_obj) return (Item){.item = b2it(false)};
        Item recv_own = js_object_get_own_property_descriptor(receiver, key);
        if (get_type_id(recv_own) == LMD_TYPE_MAP) {
            bool r_has_set = false, r_has_get = false;
            js_map_get_fast_ext(recv_own.map,
                it2s(set_key)->chars, (int)it2s(set_key)->len, &r_has_set);
            js_map_get_fast_ext(recv_own.map,
                it2s(get_key)->chars, (int)it2s(get_key)->len, &r_has_get);
            if (r_has_set || r_has_get) return (Item){.item = b2it(false)};
            bool rh_w = false;
            Item rw = js_map_get_fast_ext(recv_own.map,
                it2s(writable_key)->chars, (int)it2s(writable_key)->len, &rh_w);
            bool r_writable = rh_w ? it2b(js_to_boolean(rw)) : true;
            if (!r_writable) return (Item){.item = b2it(false)};
        }
        js_property_set(receiver, key, value);
        return (Item){.item = b2it(true)};
    }
}

// Reflect.defineProperty(obj, key, desc) — returns boolean (no throw)
extern "C" Item js_reflect_define_property(Item obj, Item key, Item desc) {
    // ES §28.1.3 Reflect.defineProperty: target must be an Object.
    if (!js_require_object_type(obj, "defineProperty")) return ItemNull;
    // step 2: Let key be ? ToPropertyKey(propertyKey).
    key = js_to_property_key(key);
    if (js_check_exception()) return ItemNull;
    if (js_is_proxy(obj)) {
        extern Item js_proxy_trap_define_property(Item proxy, Item key, Item desc);
        Item result = js_proxy_trap_define_property(obj, key, desc);
        if (get_type_id(result) == LMD_TYPE_BOOL) return result;
        return (Item){.item = b2it(it2b(js_to_boolean(result)))};
    }
    js_object_define_property(obj, key, desc);
    return (Item){.item = b2it(true)};
}

// Reflect.deleteProperty(obj, key) — returns boolean
extern "C" Item js_reflect_delete_property(Item obj, Item key) {
    // ES §28.1.4 Reflect.deleteProperty: target must be an Object.
    if (!js_require_object_type(obj, "deleteProperty")) return ItemNull;
    // step 2: Let key be ? ToPropertyKey(propertyKey).
    key = js_to_property_key(key);
    if (js_check_exception()) return ItemNull;
    return js_delete_property(obj, key);
}

// Reflect.setPrototypeOf(obj, proto) — returns boolean
extern "C" Item js_reflect_set_prototype_of(Item obj, Item proto) {
    // ES §28.1.15 Reflect.setPrototypeOf: target must be an Object.
    if (!js_require_object_type(obj, "setPrototypeOf")) return ItemNull;
    // proto must be Object or null; otherwise TypeError (covers Symbol too).
    TypeId pt = get_type_id(proto);
    bool proto_is_null = (proto.item == ItemNull.item);
    bool proto_is_obj = (pt == LMD_TYPE_MAP || pt == LMD_TYPE_FUNC ||
                        pt == LMD_TYPE_ARRAY || pt == LMD_TYPE_ELEMENT);
    if (!proto_is_null && !proto_is_obj) {
        js_throw_type_error("Object prototype may only be an Object or null");
        return ItemNull;
    }
    if (js_is_proxy(obj)) {
        extern Item js_proxy_trap_set_prototype_of(Item proxy, Item proto);
        return js_proxy_trap_set_prototype_of(obj, proto);
    }
    // OrdinarySetPrototypeOf (ES §10.1.2.1):
    // 4. If SameValue(proto, current) is true, return true.
    Item current = js_get_prototype_of(obj);
    if (current.item == proto.item) return (Item){.item = b2it(true)};
    // 5. If [[Extensible]] is false, return false.
    bool extensible = it2b(js_to_boolean(js_object_is_extensible(obj)));
    if (!extensible) return (Item){.item = b2it(false)};
    // 8. Cycle check: walk proto chain; if it contains target, return false.
    if (proto_is_obj) {
        Item p = proto;
        int depth = 0;
        while (p.item != ItemNull.item && depth < 100) {
            if (p.item == obj.item) return (Item){.item = b2it(false)};
            if (js_is_proxy(p)) break;  // would need trap; skip
            p = js_get_prototype_of(p);
            depth++;
        }
    }
    js_set_prototype(obj, proto);
    return (Item){.item = b2it(true)};
}

// Object.setPrototypeOf(obj, proto) — ES §20.1.2.21
// Differences from Reflect.setPrototypeOf:
// - target null/undefined → TypeError (RequireObjectCoercible)
// - proto not Object/null → TypeError (already true above)
// - SetPrototypeOf returning false (cycle/non-extensible) → TypeError
// - Returns the (possibly-coerced) object on success.
extern "C" Item js_object_set_prototype_of(Item obj, Item proto) {
    // 1. RequireObjectCoercible
    if (obj.item == ItemNull.item || obj.item == ITEM_JS_UNDEFINED) {
        js_throw_type_error("Object.setPrototypeOf called on null or undefined");
        return ItemNull;
    }
    // 2. proto must be Object or null (undefined throws TypeError).
    TypeId pt = get_type_id(proto);
    bool proto_is_null = (proto.item == ItemNull.item);
    bool proto_is_obj = (pt == LMD_TYPE_MAP || pt == LMD_TYPE_FUNC ||
                        pt == LMD_TYPE_ARRAY || pt == LMD_TYPE_ELEMENT);
    if (!proto_is_null && !proto_is_obj) {
        js_throw_type_error("Object prototype may only be an Object or null");
        return ItemNull;
    }
    // 3. If O is not Object, return O (primitives pass through).
    TypeId ot = get_type_id(obj);
    if (ot != LMD_TYPE_MAP && ot != LMD_TYPE_FUNC && ot != LMD_TYPE_ARRAY && ot != LMD_TYPE_ELEMENT) {
        return obj;
    }
    // 4. Delegate to Reflect.setPrototypeOf semantics; throw on false.
    Item r = js_reflect_set_prototype_of(obj, proto);
    if (js_check_exception()) return ItemNull;
    if (r.item == (uint64_t)b2it(false)) {
        js_throw_type_error("Object.setPrototypeOf: cyclic __proto__ value or non-extensible target");
        return ItemNull;
    }
    return obj;
}

// Reflect.preventExtensions(obj) — returns boolean
extern "C" Item js_reflect_prevent_extensions(Item obj) {
    // ES §28.1.12 Reflect.preventExtensions: target must be an Object.
    if (!js_require_object_type(obj, "preventExtensions")) return ItemNull;
    if (js_is_proxy(obj)) {
        extern Item js_proxy_trap_prevent_extensions(Item proxy);
        Item result = js_proxy_trap_prevent_extensions(obj);
        if (get_type_id(result) == LMD_TYPE_BOOL) return result;
        return (Item){.item = b2it(it2b(js_to_boolean(result)))};
    }
    js_object_prevent_extensions(obj);
    return (Item){.item = b2it(true)};
}

// Reflect.get(target, key [, receiver]) — ES §28.1.6
extern "C" Item js_reflect_get(Item target, Item key) {
    if (!js_require_object_type(target, "get")) return ItemNull;
    key = js_to_property_key(key);
    if (js_check_exception()) return ItemNull;
    return js_property_access(target, key);
}

// Reflect.has(target, key) — ES §28.1.9
extern "C" Item js_reflect_has(Item target, Item key) {
    if (!js_require_object_type(target, "has")) return ItemNull;
    key = js_to_property_key(key);
    if (js_check_exception()) return ItemNull;
    return js_in(key, target);
}

// Reflect.getPrototypeOf(target) — ES §28.1.8
extern "C" Item js_reflect_get_prototype_of(Item target) {
    if (!js_require_object_type(target, "getPrototypeOf")) return ItemNull;
    return js_get_prototype_of(target);
}

// Reflect.isExtensible(target) — ES §28.1.10
extern "C" Item js_reflect_is_extensible(Item target) {
    if (!js_require_object_type(target, "isExtensible")) return ItemNull;
    return js_object_is_extensible(target);
}

// Reflect.getOwnPropertyDescriptor(target, key) — ES §28.1.7
extern "C" Item js_reflect_get_own_property_descriptor(Item target, Item key) {
    if (!js_require_object_type(target, "getOwnPropertyDescriptor")) return ItemNull;
    key = js_to_property_key(key);
    if (js_check_exception()) return ItemNull;
    return js_object_get_own_property_descriptor(target, key);
}

// Reflect.apply(target, thisArg, argsList) — call target with thisArg and args
extern "C" Item js_reflect_apply(Item target, Item this_arg, Item args_array) {
    // Proxy wrapping a callable: forward through apply trap
    if (js_is_proxy(target)) {
        int argc = 0;
        Item* args = NULL;
        if (get_type_id(args_array) == LMD_TYPE_ARRAY) {
            argc = (int)js_array_length(args_array);
            if (argc > 0) {
                args = (Item*)alloca(argc * sizeof(Item));
                for (int i = 0; i < argc; i++) {
                    Item idx = {.item = i2it(i)};
                    args[i] = js_array_get(args_array, idx);
                }
            }
        }
        return js_proxy_trap_apply(target, this_arg, args, argc);
    }
    if (get_type_id(target) != LMD_TYPE_FUNC) {
        Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item em = (Item){.item = s2it(heap_create_name("Reflect.apply requires a function"))};
        js_throw_value(js_new_error_with_name(tn, em));
        return ItemNull;
    }
    int argc = 0;
    Item* args = NULL;
    if (get_type_id(args_array) == LMD_TYPE_ARRAY) {
        argc = (int)js_array_length(args_array);
        if (argc > 0) {
            args = (Item*)alloca(argc * sizeof(Item));
            for (int i = 0; i < argc; i++) {
                Item idx = {.item = i2it(i)};
                args[i] = js_array_get(args_array, idx);
            }
        }
    }
    return js_call_function(target, this_arg, args, argc);
}

// =============================================================================
// Object.getOwnPropertyDescriptor — return descriptor for an own property
// =============================================================================

// Forward declarations for array companion map helpers (defined before defineProperty)
static Map* js_array_props_map(Array* arr);
static Item js_defprop_get_marker(Item obj, const char* key, int keylen, bool* found);

// v18: partial JsFunction layout for properties_map access
struct JsFuncProps {
    TypeId type_id;
    void* func_ptr;
    int param_count;
    Item* env;
    int env_size;
    Item prototype;
    Item bound_this;
    Item* bound_args;
    int bound_argc;
    String* name;
    int builtin_id;
    Item properties_map;
};

// ES spec: built-in constructor's `prototype` data property is non-writable,
// non-enumerable, non-configurable. This helper is consulted by both
// js_object_get_own_property_descriptor (descriptor synthesis) and
// js_property_set (to silently reject writes to a non-writable prototype).
extern "C" bool js_func_is_builtin_ctor(Item fn) {
    if (get_type_id(fn) != LMD_TYPE_FUNC) return false;
    JsFuncProps* efn = (JsFuncProps*)fn.function;
    if (!efn->name) return false;
    const char* en = efn->name->chars;
    int el = (int)efn->name->len;
    return (el == 5 && strncmp(en, "Error", 5) == 0) ||
        (el == 9 && strncmp(en, "TypeError", 9) == 0) ||
        (el == 10 && strncmp(en, "RangeError", 10) == 0) ||
        (el == 14 && strncmp(en, "ReferenceError", 14) == 0) ||
        (el == 11 && strncmp(en, "SyntaxError", 11) == 0) ||
        (el == 8 && strncmp(en, "URIError", 8) == 0) ||
        (el == 9 && strncmp(en, "EvalError", 9) == 0) ||
        (el == 3 && strncmp(en, "Map", 3) == 0) ||
        (el == 3 && strncmp(en, "Set", 3) == 0) ||
        (el == 7 && strncmp(en, "WeakMap", 7) == 0) ||
        (el == 7 && strncmp(en, "WeakSet", 7) == 0) ||
        (el == 7 && strncmp(en, "Promise", 7) == 0) ||
        (el == 11 && strncmp(en, "ArrayBuffer", 11) == 0) ||
        (el == 8 && strncmp(en, "DataView", 8) == 0) ||
        (el == 5 && strncmp(en, "Array", 5) == 0) ||
        (el == 7 && strncmp(en, "Boolean", 7) == 0) ||
        (el == 6 && (strncmp(en, "Number", 6) == 0 || strncmp(en, "String", 6) == 0 || strncmp(en, "Object", 6) == 0 || strncmp(en, "RegExp", 6) == 0 || strncmp(en, "Symbol", 6) == 0)) ||
        (el == 4 && strncmp(en, "Date", 4) == 0) ||
        (el == 8 && strncmp(en, "Function", 8) == 0) ||
        (el == 17 && strncmp(en, "GeneratorFunction", 17) == 0) ||
        (el == 22 && strncmp(en, "AsyncGeneratorFunction", 22) == 0) ||
        (el == 13 && strncmp(en, "AsyncFunction", 13) == 0) ||
        js_is_typed_array_ctor_name(en, el);
}

extern "C" Item js_object_get_own_property_descriptor(Item obj, Item name) {
    // Proxy [[GetOwnProperty]] trap
    if (js_is_proxy(obj)) {
        return js_proxy_trap_get_own_property_descriptor(obj, name);
    }
    // v20: GOPD should accept primitives (ES spec uses ToObject internally)
    // Only null/undefined throw TypeError
    TypeId type = get_type_id(obj);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED ||
        (obj.item == 0 && type != LMD_TYPE_INT)) {
        extern Item js_new_error_with_name(Item type_name, Item message);
        extern void js_throw_value(Item error);
        Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item msg = (Item){.item = s2it(heap_create_name("Cannot convert undefined or null to object"))};
        js_throw_value(js_new_error_with_name(tn, msg));
        return ItemNull;
    }

    // Convert name to string for comparison
    // v41: Symbol keys must be converted to __sym_N internal format, not human-readable string
    Item name_str_item;
    if (get_type_id(name) == LMD_TYPE_INT && it2i(name) <= -(int64_t)JS_SYMBOL_BASE) {
        int64_t id = -(it2i(name) + (int64_t)JS_SYMBOL_BASE);
        char buf[32];
        snprintf(buf, sizeof(buf), "__sym_%lld", (long long)id);
        name_str_item = (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
    } else {
        name_str_item = js_to_string(name);
    }
    if (get_type_id(name_str_item) != LMD_TYPE_STRING) return ItemNull;
    String* name_str = it2s(name_str_item);
    // ES §7.1.19 ToPropertyKey: name is already coerced; replace `name` so all
    // downstream lookups (js_has_own_property, js_property_get, etc.) use the
    // coerced string key rather than the raw input (which may be an object
    // whose toString returns the actual key — see test
    // built-ins/Object/getOwnPropertyDescriptor/15.2.3.3-2-42).
    name = name_str_item;

    // J39-7: ES §B.2.2.1 / §10.4.7 — the `__proto__` slot is the [[Prototype]]
    // internal slot, NOT an own property of plain objects. Object literal
    // `{__proto__: x}` and `Object.create(proto)` both store the proto via this
    // slot but the spec says `Object.getOwnPropertyDescriptor(o, '__proto__')`
    // must return undefined unless `__proto__` was explicitly created as an
    // accessor or data property (e.g. via accessor syntax `get __proto__()`,
    // `set __proto__(_)`, or `Object.defineProperty`). Suppress descriptor
    // synthesis only when the slot is the [[Prototype]] storage (no IS_ACCESSOR
    // shape flag for `__proto__`).
    if (type == LMD_TYPE_MAP && name_str->len == 9 &&
        memcmp(name_str->chars, "__proto__", 9) == 0) {
        ShapeEntry* _se_pp = js_find_shape_entry(obj, "__proto__", 9);
        if (!_se_pp || !jspd_is_accessor(_se_pp)) {
            return make_js_undefined();
        }
        // fall through: explicit own accessor — return real descriptor below
    }

    // Function properties: length, name, prototype
    if (type == LMD_TYPE_FUNC) {
        // Stage A2.2: route FUNC own-property descriptor synthesis through
        // the unified inspector. Handles both IS_ACCESSOR and legacy
        // __get_/__set_ accessor patterns and the data-property attribute
        // markers in fn->properties_map. Falls through to the synthetic
        // length/name/prototype intrinsics below when no own property is
        // present in fn->properties_map.
        {
            JsPropertyDescriptor pd = {};
            if (js_get_own_property_descriptor(obj, name_str->chars,
                                                (int)name_str->len, &pd)) {
                Item desc = js_new_object();
                if (js_pd_is_accessor(&pd)) {
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("get", 3))},
                                    (pd.flags & JS_PD_HAS_GET) ? pd.getter : make_js_undefined());
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("set", 3))},
                                    (pd.flags & JS_PD_HAS_SET) ? pd.setter : make_js_undefined());
                } else {
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))},
                                    (pd.flags & JS_PD_HAS_VALUE) ? pd.value : make_js_undefined());
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))},
                                    (Item){.item = b2it((pd.flags & JS_PD_WRITABLE) != 0)});
                }
                js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))},
                                (Item){.item = b2it((pd.flags & JS_PD_ENUMERABLE) != 0)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))},
                                (Item){.item = b2it(js_pd_is_configurable(&pd))});
                return desc;
            }
            // Special case: properties_map may carry a deleted-sentinel for
            // "prototype" meaning "fall through to fn->prototype synth".
            // js_get_own_property_descriptor returns false on sentinel, so
            // we re-check explicitly below for that key.
            JsFuncProps* fn = (JsFuncProps*)obj.function;
            if (fn->properties_map.item != 0 &&
                name_str->len == 9 &&
                strncmp(name_str->chars, "prototype", 9) == 0) {
                bool own = false;
                Item val = js_map_get_fast_ext(fn->properties_map.map,
                                                name_str->chars, name_str->len, &own);
                if (own && val.item == JS_DELETED_SENTINEL_VAL) {
                    goto func_prototype_synth;
                }
                if (own) return make_js_undefined();
            }
        }
        if (name_str->len == 6 && strncmp(name_str->chars, "length", 6) == 0) {
            Item value = js_property_get(obj, name);
            Item desc = js_new_object();
            js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, value);
            js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
            return desc;
        }
        if (name_str->len == 4 && strncmp(name_str->chars, "name", 4) == 0) {
            Item name_val = js_property_get(obj, name);
            Item desc = js_new_object();
            js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, name_val);
            js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
            return desc;
        }
        if (name_str->len == 9 && strncmp(name_str->chars, "prototype", 9) == 0) {
            func_prototype_synth:
            // Only constructor functions have prototype as own property
            if (!js_func_has_own_prototype(obj)) return make_js_undefined();
            Item desc = js_new_object();
            Item proto = js_property_get(obj, name);
            js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, proto);
            bool is_builtin_ctor = js_func_is_builtin_ctor(obj);
            js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(!is_builtin_ctor)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(false)});
            return desc;
        }
        // v18: check custom properties backing map
        {
            JsFuncProps* fn = (JsFuncProps*)obj.function;
            if (fn->properties_map.item != 0) {
                bool own = false;
                Item val = js_map_get_fast_ext(fn->properties_map.map, name_str->chars, name_str->len, &own);
                if (own) {
                    Item desc = js_new_object();
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, val);
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(true)});
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(true)});
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
                    return desc;
                }
            }
        }
        return make_js_undefined();
    }

    // String properties: length, numeric indices
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(obj);
        if (name_str->len == 6 && strncmp(name_str->chars, "length", 6) == 0) {
            Item desc = js_new_object();
            js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, (Item){.item = i2it(s->len)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(false)});
            return desc;
        }
        // numeric index → character at that position
        if (name_str->len > 0 && name_str->chars[0] >= '0' && name_str->chars[0] <= '9') {
            int idx = 0;
            bool valid = true;
            for (int i = 0; i < (int)name_str->len; i++) {
                if (name_str->chars[i] < '0' || name_str->chars[i] > '9') { valid = false; break; }
                idx = idx * 10 + (name_str->chars[i] - '0');
            }
            if (valid && idx >= 0 && idx < (int)s->len) {
                Item ch = item_at(obj, (int64_t)idx);
                Item desc = js_new_object();
                js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, ch);
                js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(false)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(true)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(false)});
                return desc;
            }
        }
        return make_js_undefined();
    }

    // Array properties: length, numeric indices
    if (type == LMD_TYPE_ARRAY) {
        if (name_str->len == 6 && strncmp(name_str->chars, "length", 6) == 0) {
            // J39-7: honor __nw_length marker (set by defineProperty(arr, "length", {writable:false}))
            bool writable = true;
            if (obj.array && obj.array->extra != 0) {
                Map* pm = (Map*)(uintptr_t)obj.array->extra;
                bool nw_found = false;
                Item nwv = js_map_get_fast_ext(pm, "__nw_length", 11, &nw_found);
                if (nw_found && nwv.item != JS_DELETED_SENTINEL_VAL && js_is_truthy(nwv)) writable = false;
            }
            Item desc = js_new_object();
            js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, (Item){.item = i2it(obj.array->length)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(writable)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(false)});
            return desc;
        }
        // numeric index
        if (name_str->len > 0 && name_str->chars[0] >= '0' && name_str->chars[0] <= '9') {
            int idx = 0;
            for (int i = 0; i < (int)name_str->len; i++) {
                if (name_str->chars[i] < '0' || name_str->chars[i] > '9') { idx = -1; break; }
                idx = idx * 10 + (name_str->chars[i] - '0');
            }
            // check for accessor properties in companion map (even when idx >= length)
            if (idx >= 0 && obj.array->extra != 0) {
                Map* props = (Map*)(uintptr_t)obj.array->extra;
                // Phase 5D: IS_ACCESSOR shape-flag dispatch under digit-string name.
                Item pm_item = (Item){.map = props};
                ShapeEntry* _se_idx = js_find_shape_entry(pm_item, name_str->chars, (int)name_str->len);
                if (_se_idx && jspd_is_accessor(_se_idx)) {
                    bool slot_found = false;
                    Item slot_val = js_map_get_fast_ext(props, name_str->chars, (int)name_str->len, &slot_found);
                    if (slot_found && slot_val.item != JS_DELETED_SENTINEL_VAL) {
                        JsAccessorPair* pair = js_item_to_accessor_pair(slot_val);
                        Item desc = js_new_object();
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("get", 3))},
                            (pair && pair->getter.item != ItemNull.item) ? pair->getter : make_js_undefined());
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("set", 3))},
                            (pair && pair->setter.item != ItemNull.item) ? pair->setter : make_js_undefined());
                        bool is_enumerable = jspd_is_enumerable(_se_idx);
                        bool is_configurable = jspd_is_configurable(_se_idx);
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(is_enumerable)});
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(is_configurable)});
                        return desc;
                    }
                }
                // AT-3: legacy __get_<idx>/__set_<idx> marker fallback retired
                // (post-AT-1 IS_ACCESSOR shape probe above always succeeds).
            }
            if (idx >= 0 && idx < obj.array->length) {
                // v25: deleted elements (holes) have no descriptor
                if (obj.array->items[idx].item == JS_DELETED_SENTINEL_VAL) {
                    return make_js_undefined();
                }
                Item desc = js_new_object();
                js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, obj.array->items[idx]);
                // Stage A3.2: shape-flag-first attribute query.
                ShapeEntry* _se = js_find_shape_entry(obj, name_str->chars, (int)name_str->len);
                Map* arr_props = obj.array->extra ? (Map*)(uintptr_t)obj.array->extra : NULL;
                bool is_writable = js_props_query_writable(arr_props, _se, name_str->chars, (int)name_str->len);
                bool is_configurable = js_props_query_configurable(arr_props, _se, name_str->chars, (int)name_str->len);
                bool is_enumerable = js_props_query_enumerable(arr_props, _se, name_str->chars, (int)name_str->len);
                js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(is_writable)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(is_enumerable)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(is_configurable)});
                return desc;
            }
        }
        // Named properties on companion map (e.g., arguments.callee, Symbol.toStringTag)
        if (obj.array->extra) {
            Map* companion = (Map*)(uintptr_t)obj.array->extra;
            Item comp_item = (Item){.map = companion};
            Item name_key = (Item){.item = s2it(heap_create_name(name_str->chars, name_str->len))};
            Item val = js_has_own_property(comp_item, name_key);
            if (js_is_truthy(val)) {
                Item prop_val = js_property_get(comp_item, name_key);
                Item desc = js_new_object();
                js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, prop_val);
                // Stage A3.2: shape-flag-first attribute query on companion map.
                ShapeEntry* _se = js_find_shape_entry(comp_item, name_str->chars, (int)name_str->len);
                bool wr = js_props_query_writable(companion, _se, name_str->chars, (int)name_str->len);
                bool cf = js_props_query_configurable(companion, _se, name_str->chars, (int)name_str->len);
                bool en = js_props_query_enumerable(companion, _se, name_str->chars, (int)name_str->len);
                js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(wr)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(en)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(cf)});
                return desc;
            }
        }
        return make_js_undefined();
    }

    // Map (object) properties
    if (type == LMD_TYPE_MAP) {
        Map* m = obj.map;
        if (!m || !m->type) return make_js_undefined();

        // Stage A2.1: route accessor/legacy-marker descriptor synthesis
        // through unified js_get_own_property_descriptor inspector. Falls
        // through to the data-property + virtual-builtin paths below when
        // the kernel reports an own data property (or no own property at
        // all) — those paths still depend on `__class_name__` and shape
        // shape semantics that A2 has not yet collapsed.
        {
            JsPropertyDescriptor pd = {};
            if (js_get_own_property_descriptor(obj, name_str->chars,
                                                (int)name_str->len, &pd)) {
                if (js_pd_is_accessor(&pd)) {
                    Item desc = js_new_object();
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("get", 3))},
                                    (pd.flags & JS_PD_HAS_GET) ? pd.getter : make_js_undefined());
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("set", 3))},
                                    (pd.flags & JS_PD_HAS_SET) ? pd.setter : make_js_undefined());
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))},
                                    (Item){.item = b2it((pd.flags & JS_PD_ENUMERABLE) != 0)});
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))},
                                    (Item){.item = b2it(js_pd_is_configurable(&pd))});
                    return desc;
                }
                // Data descriptor — fall through to the legacy data-property
                // path (which handles __class_name__ virtual-builtin synth).
            }
        }

        // Check for own data property
        Item has_own = js_has_own_property(obj, name);
        if (!it2b(has_own)) {
            return make_js_undefined();
        }

        // v26: Check if the property is actually stored in the map, or if it's a
        // virtual builtin method from __class_name__ resolution. Builtins should have
        // enumerable:false, configurable:true.
        {
            bool stored = false;
            js_map_get_fast_ext(m, name_str->chars, (int)name_str->len, &stored);
            if (!stored) {
                // Not stored = virtual builtin method (only on prototypes)
                bool ip_own = false;
                js_map_get_fast_ext(m, "__is_proto__", 12, &ip_own);
                if (ip_own) {
                    JsClass cls = js_class_id((Item){.map = m});
                    TypeId lookup_type = (TypeId)0;
                    if (cls == JS_CLASS_ARRAY) lookup_type = LMD_TYPE_ARRAY;
                    else if (cls == JS_CLASS_STRING) lookup_type = LMD_TYPE_STRING;
                    else if (cls == JS_CLASS_FUNCTION) lookup_type = LMD_TYPE_FUNC;
                    else if (cls == JS_CLASS_NUMBER) lookup_type = LMD_TYPE_INT;
                    else if (cls == JS_CLASS_BOOLEAN) lookup_type = LMD_TYPE_BOOL;

                    Item builtin = ItemNull;
                    if (lookup_type != (TypeId)0) {
                        builtin = js_lookup_builtin_method(lookup_type, name_str->chars, (int)name_str->len);
                    }
                    if (builtin.item == ItemNull.item) {
                        builtin = js_lookup_builtin_method(LMD_TYPE_MAP, name_str->chars, (int)name_str->len);
                    }
                    // Symbol.iterator (__sym_1) — virtual property on Array/String prototypes
                    if (builtin.item == ItemNull.item && name_str->len == 7 && strncmp(name_str->chars, "__sym_1", 7) == 0) {
                        if (lookup_type == LMD_TYPE_ARRAY || lookup_type == LMD_TYPE_STRING) {
                            builtin = js_property_get(obj, name);
                        }
                    }
                    // fallback: check via js_property_get for class-name-based methods
                    // (Date, RegExp, Error, Map, Set, etc.) not in js_lookup_builtin_method
                    // Use js_property_get on the map directly — class-name resolution happens
                    // at the MAP level without following __proto__, so this is safe for own-check
                    if (builtin.item == ItemNull.item && js_map_has_builtin_method(m, name_str->chars, (int)name_str->len)) {
                        builtin = js_property_get(obj, name);
                    }
                    if (builtin.item != ItemNull.item) {
                        Item desc = js_new_object();
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, builtin);
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(true)});
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
                        return desc;
                    }
                    // "constructor" — always has a descriptor if hasOwn returned true
                    if (name_str->len == 11 && strncmp(name_str->chars, "constructor", 11) == 0) {
                        Item value = js_property_get(obj, name);
                        Item desc = js_new_object();
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, value);
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(true)});
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
                        return desc;
                    }
                } // ip_own
            }
        }

        Item value = js_property_get(obj, name);
        Item desc = js_new_object();
        js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, value);
        // ES §10.4.3.4 String exotic [[GetOwnProperty]]: length and integer-index
        // properties up to length have {writable:false, enumerable:true (indices) /
        // false (length), configurable:false}.
        if (js_class_id((Item){.map = m}) == JS_CLASS_STRING) {
            bool sw_is_length = (name_str->len == 6 && memcmp(name_str->chars, "length", 6) == 0);
            bool sw_is_index = false;
            if (!sw_is_length && name_str->len > 0) {
                bool all_digits = true;
                for (int i = 0; i < (int)name_str->len; i++) {
                    if (name_str->chars[i] < '0' || name_str->chars[i] > '9') { all_digits = false; break; }
                }
                if (all_digits && (name_str->len == 1 || name_str->chars[0] != '0')) {
                    bool own_pv = false;
                    Item pv = js_map_get_fast_ext(m, "__primitiveValue__", 18, &own_pv);
                    if (own_pv && get_type_id(pv) == LMD_TYPE_STRING) {
                        String* pv_s = it2s(pv);
                        if (pv_s) {
                            long idx = strtol(name_str->chars, NULL, 10);
                            if (idx >= 0 && idx < (long)pv_s->len) sw_is_index = true;
                        }
                    }
                }
            }
            if (sw_is_length) {
                js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(false)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(false)});
                return desc;
            }
            if (sw_is_index) {
                js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(false)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(true)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(false)});
                return desc;
            }
        }
        // Stage A3.2: shape-flag-first attribute query.
        ShapeEntry* _se = js_find_shape_entry(obj, name_str->chars, (int)name_str->len);
        bool is_writable = js_props_query_writable(m, _se, name_str->chars, (int)name_str->len);
        bool is_configurable = js_props_query_configurable(m, _se, name_str->chars, (int)name_str->len);
        bool is_enumerable = js_props_query_enumerable(m, _se, name_str->chars, (int)name_str->len);
        js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(is_writable)});
        js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(is_enumerable)});
        js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(is_configurable)});
        return desc;
    }

    return make_js_undefined();
}

// =============================================================================
// Object.getOwnPropertyDescriptors — return descriptors for all own properties
// =============================================================================

extern "C" Item js_object_get_own_property_descriptors(Item obj) {
    // ES6: primitives get ToObject; null/undefined throw
    TypeId ot = get_type_id(obj);
    if (ot == LMD_TYPE_INT || ot == LMD_TYPE_FLOAT || ot == LMD_TYPE_BOOL) return js_new_object();
    if (ot == LMD_TYPE_STRING) {
        // String: describe each character index + length
        // Delegate to getOwnPropertyNames + getOwnPropertyDescriptor
        Item result = js_new_object();
        String* str = it2s(obj);
        int slen = str ? (int)str->len : 0;
        for (int i = 0; i < slen; i++) {
            char buf[16]; snprintf(buf, sizeof(buf), "%d", i);
            Item key = (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
            Item desc = js_new_object();
            js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))},
                (Item){.item = s2it(heap_create_name(str->chars + i, 1))});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(true)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(false)});
            js_property_set(result, key, desc);
        }
        return result;
    }
    if (!js_require_object_type(obj, "getOwnPropertyDescriptors")) return ItemNull;
    Item result = js_new_object();
    Item keys = js_object_get_own_property_names(obj);
    if (get_type_id(keys) != LMD_TYPE_ARRAY) return result;
    for (int i = 0; i < keys.array->length; i++) {
        Item key = keys.array->items[i];
        Item desc = js_object_get_own_property_descriptor(obj, key);
        if (desc.item != make_js_undefined().item) {
            js_property_set(result, key, desc);
        }
    }
    return result;
}

// =============================================================================
// Array companion property map (stored in arr->extra)
// Arrays don't have Map storage for arbitrary string keys, so attribute markers
// (__nw_, __nc_, __ne_, __get_, __set_) are stored in a lazily-created companion Map.
// =============================================================================

static Map* js_array_props_map(Array* arr) {
    if (arr->extra == 0) return NULL;
    return (Map*)(uintptr_t)arr->extra;
}

static Map* js_array_ensure_props_map(Array* arr) {
    if (arr->extra == 0) {
        Item obj = js_new_object();
        // Tag as companion map so the Phase 4 accessor-marker intercept skips
        // it and lets legacy __get_N/__set_N keys land literally — the array
        // index reader looks them up by literal key in the companion map.
        obj.map->map_kind = MAP_KIND_ARRAY_PROPS;
        arr->extra = (int64_t)(uintptr_t)obj.map;
    }
    return (Map*)(uintptr_t)arr->extra;
}

// store a marker (__nw_, __nc_, __ne_, __get_, __set_) on an object
// for arrays, routes to companion map instead of js_property_set
//
// Phase 2a: ShapeEntry::flags dual-write is now handled centrally by
// `js_dual_write_marker_flags` invoked at the top of `js_property_set` —
// no per-callsite mirroring needed here.
extern "C" void js_defprop_set_marker(Item obj, Item key, Item value) {
    if (get_type_id(obj) == LMD_TYPE_ARRAY) {
        Map* m = js_array_ensure_props_map(obj.array);
        Item map_item = (Item){.map = m};
        js_property_set(map_item, key, value);
    } else {
        js_property_set(obj, key, value);
    }
}

// check if a marker exists on an object (for arrays, checks companion map)
extern "C" bool js_defprop_has_marker(Item obj, Item key) {
    if (get_type_id(obj) == LMD_TYPE_ARRAY) {
        Map* m = js_array_props_map(obj.array);
        if (!m) return false;
        String* ks = it2s(key);
        if (!ks) return false;
        bool found = false;
        Item v = js_map_get_fast_ext(m, ks->chars, (int)ks->len, &found);
        // Tombstoned marker (cleared by delete) is treated as absent.
        if (found && v.item == JS_DELETED_SENTINEL_VAL) return false;
        return found;
    }
    if (it2b(js_has_own_property(obj, key))) {
        Item v = js_property_get(obj, key);
        if (v.item == JS_DELETED_SENTINEL_VAL) return false;
        return true;
    }
    return false;
}

// read a marker value from an object (for arrays, reads from companion map; for functions, from properties_map)
static Item js_defprop_get_marker(Item obj, const char* key, int keylen, bool* found) {
    Item v = ItemNull;
    if (get_type_id(obj) == LMD_TYPE_ARRAY) {
        Map* m = js_array_props_map(obj.array);
        if (!m) { *found = false; return ItemNull; }
        v = js_map_get_fast_ext(m, key, keylen, found);
    } else if (get_type_id(obj) == LMD_TYPE_FUNC) {
        JsFuncProps* fn = (JsFuncProps*)obj.function;
        if (fn->properties_map.item != 0 && get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
            v = js_map_get_fast_ext(fn->properties_map.map, key, keylen, found);
        } else {
            *found = false;
            return ItemNull;
        }
    } else {
        v = js_map_get_fast_ext(obj.map, key, keylen, found);
    }
    // Tombstoned marker (cleared by delete) is treated as absent.
    if (*found && v.item == JS_DELETED_SENTINEL_VAL) {
        *found = false;
        return ItemNull;
    }
    return v;
}

// =============================================================================
// Object.defineProperty — define a property on an object
// =============================================================================

extern "C" Item js_object_define_property(Item obj, Item name, Item descriptor) {
    // Proxy [[DefineOwnProperty]] trap
    if (js_is_proxy(obj)) {
        return js_proxy_trap_define_property(obj, name, descriptor);
    }
    if (!js_require_object_type(obj, "defineProperty")) return ItemNull;
    if (obj.item == 0) return obj;

    // v18l: Non-extensible check — cannot add new properties to non-extensible objects
    TypeId obj_type = get_type_id(obj);
    if (obj_type == LMD_TYPE_MAP || obj_type == LMD_TYPE_ARRAY) {
        Item is_ext = js_object_is_extensible(obj);
        if (!js_is_truthy(is_ext)) {
            // Coerce name for property existence check
            Item check_name = js_to_property_key(name);
            Item has = js_has_own_property(obj, check_name);
            if (!it2b(has)) {
                // Phase-5D: legacy __get_<name> probe removed. js_has_own_property
                // already returns true for IS_ACCESSOR shape entries.
                Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                Item msg = (Item){.item = s2it(heap_create_name("Cannot define property, object is not extensible"))};
                js_throw_value(js_new_error_with_name(tn, msg));
                return obj;
            }
        }
    }

    return ValidateAndApplyPropertyDescriptor(obj, name, descriptor);
}

// =============================================================================
// Object.defineProperties — define multiple properties on an object
// =============================================================================

extern "C" Item js_object_define_properties(Item obj, Item props) {
    if (!js_require_object_type(obj, "defineProperties")) return ItemNull;
    // ES spec §19.1.2.3 step 1: Let props be ? ToObject(Properties).
    // ToObject throws TypeError on null/undefined.
    TypeId pt = get_type_id(props);
    if (pt == LMD_TYPE_NULL || pt == LMD_TYPE_UNDEFINED) {
        return js_throw_type_error("Cannot convert undefined or null to object");
    }
    if (obj.item == 0 || (pt != LMD_TYPE_MAP && pt != LMD_TYPE_ARRAY)) return obj;
    Item keys = js_object_keys(props);
    if (get_type_id(keys) != LMD_TYPE_ARRAY) return obj;
    int n = keys.array->length;
    if (n == 0) return obj;
    // J39-7: ES §19.1.2.3 ObjectDefineProperties is two-phase per spec:
    //   Phase 1 (step 4): for each key, fetch descObj (may invoke getter) and
    //     validate via ToPropertyDescriptor — collect into descriptors list.
    //   Phase 2 (step 5): for each (key, desc), DefinePropertyOrThrow.
    // If any ToPropertyDescriptor throws in phase 1, no defines happen.
    Item* desc_objs = (Item*)malloc(sizeof(Item) * n);
    if (!desc_objs) return obj;
    for (int i = 0; i < n; i++) {
        Item key = keys.array->items[i];
        Item desc = js_property_get(props, key);
        if (js_check_exception()) { free(desc_objs); return obj; }
        JsPropertyDescriptor tmp;
        if (!js_descriptor_from_object(desc, &tmp)) {
            free(desc_objs);
            return obj; // ToPropertyDescriptor threw — abort before any define
        }
        desc_objs[i] = desc;
    }
    for (int i = 0; i < n; i++) {
        Item key = keys.array->items[i];
        js_object_define_property(obj, key, desc_objs[i]);
        if (js_check_exception()) break; // DefinePropertyOrThrow failure
    }
    free(desc_objs);
    return obj;
}

// Wrapper for Object.create(O, Properties): per ES §19.1.2.2 step 4, only call
// ObjectDefineProperties when Properties is not undefined. Object.defineProperties
// itself throws TypeError on undefined/null, but Object.create only allows undefined.
// Per ES §19.1.2.2 step 3: "If Properties is not undefined, return ? ObjectDefineProperties..."
// — null still flows through and ObjectDefineProperties step 1 ToObject throws TypeError.
extern "C" Item js_object_create_define_properties(Item obj, Item props) {
    TypeId pt = get_type_id(props);
    if (pt == LMD_TYPE_UNDEFINED) return obj;
    return js_object_define_properties(obj, props);
}

// =============================================================================
// Array.isArray — check if value is an array
// =============================================================================

extern "C" Item js_array_is_array(Item value) {
    // ES spec §7.2.2: unwrap Proxy recursively
    if (js_is_proxy(value)) {
        value = js_proxy_get_target(value);
        if (value.item == ItemNull.item) return (Item){.item = ITEM_FALSE}; // revoked proxy
    }
    TypeId type = get_type_id(value);
    // Arguments exotic objects use LMD_TYPE_ARRAY internally but are not arrays per spec
    if (type == LMD_TYPE_ARRAY && value.array->is_content == 1) return (Item){.item = ITEM_FALSE};
    return (Item){.item = (type == LMD_TYPE_ARRAY) ? ITEM_TRUE : ITEM_FALSE};
}

// =============================================================================
// alert() — shim for benchmarks (outputs to console)
// =============================================================================

extern "C" Item js_alert(Item msg) {
    js_console_log(msg);
    return ItemNull;
}

// =============================================================================
// Object.keys — return array of property names
// =============================================================================

// Object.getOwnPropertyNames — includes non-enumerable own properties
extern "C" Item js_object_get_own_property_names(Item object) {
    // Proxy: forward through ownKeys trap (which handles undefined trap by forwarding to target)
    if (js_is_proxy(object)) {
        return js_proxy_trap_own_keys(object);
    }
    // ES6: ToObject for primitives
    TypeId ot = get_type_id(object);
    if (ot == LMD_TYPE_STRING) {
        String* str = it2s(object);
        int slen = str ? (int)str->len : 0;
        Item result = js_array_new(slen + 1);
        for (int i = 0; i < slen; i++) {
            char buf[16];
            int blen = snprintf(buf, sizeof(buf), "%d", i);
            js_array_set(result, (Item){.item = i2it(i)}, (Item){.item = s2it(heap_create_name(buf, blen))});
        }
        js_array_set(result, (Item){.item = i2it(slen)}, (Item){.item = s2it(heap_create_name("length", 6))});
        return result;
    }
    if (ot == LMD_TYPE_INT || ot == LMD_TYPE_FLOAT || ot == LMD_TYPE_BOOL) {
        return js_array_new(0);
    }
    if (!js_require_object_type(object, "getOwnPropertyNames")) return js_array_new(0);
    TypeId type = get_type_id(object);
    if (type == LMD_TYPE_ARRAY) {
        // indices + "length" + companion map custom properties
        int len = object.array->length;
        // v26: use push approach to skip deleted sentinel elements
        Item result = js_array_new(0);
        for (int i = 0; i < len; i++) {
            if (object.array->items[i].item == JS_DELETED_SENTINEL_VAL) continue;
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", i);
            js_array_push(result, (Item){.item = s2it(heap_create_name(buf))});
        }
        js_array_push(result, (Item){.item = s2it(heap_create_name("length", 6))});
        // v25: also include custom properties from companion map
        Map* pm = js_array_props_map(object.array);
        if (pm && pm->type) {
            TypeMap* pmt = (TypeMap*)pm->type;
            ShapeEntry* e = pmt->shape;
            while (e) {
                const char* s = e->name->str;
                int slen = (int)e->name->length;
                if (slen >= 2 && s[0] == '_' && s[1] == '_') { e = e->next; continue; }
                Item val = _map_read_field(e, pm->data);
                if (val.item == JS_DELETED_SENTINEL_VAL) { e = e->next; continue; }
                Item key_item = (Item){.item = s2it(heap_create_name(s, slen))};
                js_array_push(result, key_item);
                e = e->next;
            }
        }
        return result;
    }
    if (type == LMD_TYPE_FUNC) {
        // function own properties: length, name, [prototype] + custom from properties_map
        JsFunctionLayout* fn_lay = (JsFunctionLayout*)object.function;
        bool has_proto = js_func_has_own_prototype(object); // constructors & generators have prototype
        Item result = js_array_new(0);
        js_array_push(result, (Item){.item = s2it(heap_create_name("length", 6))});
        js_array_push(result, (Item){.item = s2it(heap_create_name("name", 4))});
        if (has_proto)
            js_array_push(result, (Item){.item = s2it(heap_create_name("prototype", 9))});
        // Include properties from properties_map (Number.NEGATIVE_INFINITY, static methods, etc.)
        JsFuncProps* fn_props = (JsFuncProps*)object.function;
        if (fn_props->properties_map.item != 0 && get_type_id(fn_props->properties_map) == LMD_TYPE_MAP) {
            Map* pm = fn_props->properties_map.map;
            if (pm && pm->type) {
                TypeMap* pmt = (TypeMap*)pm->type;
                ShapeEntry* e = pmt->shape;
                while (e) {
                    const char* s = e->name->str;
                    int slen = (int)e->name->length;
                    // skip internal markers (__ne_, __nw_, __nc_, etc.)
                    if (slen >= 2 && s[0] == '_' && s[1] == '_') { e = e->next; continue; }
                    // skip deleted sentinels
                    Item val = _map_read_field(e, pm->data);
                    if (val.item == JS_DELETED_SENTINEL_VAL) { e = e->next; continue; }
                    js_array_push(result, (Item){.item = s2it(heap_create_name(s, slen))});
                    e = e->next;
                }
            }
        }
        return result;
    }
    if (type != LMD_TYPE_MAP) return js_array_new(0);
    Map* m = object.map;
    if (!m || !m->type) return js_array_new(0);

    // v25: String wrapper objects — character indices + "length"
    {
        if (js_class_id((Item){.map = m}) == JS_CLASS_STRING) {
                bool own_pv = false;
                Item pv = js_map_get_fast_ext(m, "__primitiveValue__", 18, &own_pv);
                if (own_pv && get_type_id(pv) == LMD_TYPE_STRING) {
                    String* pv_s = it2s(pv);
                    int slen = pv_s ? (int)pv_s->len : 0;
                    Item result = js_array_new(0);
                    for (int i = 0; i < slen; i++) {
                        char buf[16];
                        int blen = snprintf(buf, sizeof(buf), "%d", i);
                        js_array_push(result, (Item){.item = s2it(heap_create_name(buf, blen))});
                    }
                    // J39-7: also include any extra own properties added after construction.
                    // Per ES §10.4.3.4 [[OwnPropertyKeys]] of String exotic: integer-index
                    // properties up to length come first, then other own properties (which
                    // includes both extra numeric indices like str[5] and named properties
                    // added via defineProperty), then inherited "length" placeholder.
                    // Pass A: extra numeric indices >= slen, in numeric order.
                    // Pass B: named (non-numeric, non-internal) shape entries.
                    TypeMap* _tm = (TypeMap*)m->type;
                    // Collect extra integer indices from shape, sort ascending.
                    int extra_idx_count = 0;
                    int extra_idx_capacity = 8;
                    int* extra_idx = (int*)alloca(extra_idx_capacity * sizeof(int));
                    {
                        ShapeEntry* se = _tm ? _tm->shape : NULL;
                        while (se) {
                            const char* s = se->name->str;
                            int len = (int)se->name->length;
                            if (len > 0 && len < 12 && s[0] >= '0' && s[0] <= '9') {
                                bool all_digit = true;
                                int v = 0;
                                for (int i = 0; i < len; i++) {
                                    if (s[i] < '0' || s[i] > '9') { all_digit = false; break; }
                                    v = v * 10 + (s[i] - '0');
                                }
                                if (all_digit && v >= slen) {
                                    Item val = _map_read_field(se, m->data);
                                    if (val.item != JS_DELETED_SENTINEL_VAL) {
                                        if (extra_idx_count >= extra_idx_capacity) {
                                            int new_cap = extra_idx_capacity * 2;
                                            int* nb = (int*)alloca(new_cap * sizeof(int));
                                            memcpy(nb, extra_idx, extra_idx_count * sizeof(int));
                                            extra_idx = nb;
                                            extra_idx_capacity = new_cap;
                                        }
                                        extra_idx[extra_idx_count++] = v;
                                    }
                                }
                            }
                            se = se->next;
                        }
                    }
                    // simple insertion sort (small N)
                    for (int i = 1; i < extra_idx_count; i++) {
                        int v = extra_idx[i]; int j = i - 1;
                        while (j >= 0 && extra_idx[j] > v) { extra_idx[j+1] = extra_idx[j]; j--; }
                        extra_idx[j+1] = v;
                    }
                    for (int i = 0; i < extra_idx_count; i++) {
                        char buf[16];
                        int blen = snprintf(buf, sizeof(buf), "%d", extra_idx[i]);
                        js_array_push(result, (Item){.item = s2it(heap_create_name(buf, blen))});
                    }
                    js_array_push(result, (Item){.item = s2it(heap_create_name("length", 6))});
                    // Pass B: named (non-numeric, non-internal) own properties.
                    {
                        ShapeEntry* se = _tm ? _tm->shape : NULL;
                        while (se) {
                            const char* s = se->name->str;
                            int len = (int)se->name->length;
                            bool skip = (len >= 2 && s[0] == '_' && s[1] == '_');
                            // skip "length" (already added) and numeric-only names
                            if (!skip && len == 6 && memcmp(s, "length", 6) == 0) skip = true;
                            if (!skip && len > 0 && s[0] >= '0' && s[0] <= '9') {
                                bool all_digit = true;
                                for (int i = 0; i < len; i++) {
                                    if (s[i] < '0' || s[i] > '9') { all_digit = false; break; }
                                }
                                if (all_digit) skip = true;
                            }
                            if (!skip) {
                                Item val = _map_read_field(se, m->data);
                                if (val.item == JS_DELETED_SENTINEL_VAL) skip = true;
                            }
                            if (!skip) {
                                js_array_push(result, (Item){.item = s2it(heap_create_name(s, len))});
                            }
                            se = se->next;
                        }
                    }
                    // v26: append builtin String method names only for prototype objects
                    bool is_proto = false;
                    js_map_get_fast_ext(m, "__is_proto__", 12, &is_proto);
                    if (is_proto) {
                        js_append_builtin_method_names(LMD_TYPE_STRING, result);
                    }
                    return result;
                }
        }
    }

    TypeMap* tm = (TypeMap*)m->type;
    // Use dynamic array approach: push matched entries to result array
    Item result = js_array_new(0);
    Array* arr = result.array;
    ShapeEntry* e = tm->shape;
    while (e) {
        const char* s = e->name->str;
        int len = (int)e->name->length;
        bool skip = (len >= 2 && s[0] == '_' && s[1] == '_');
        if (!skip) {
            Item val = _map_read_field(e, m->data);
            if (val.item == JS_DELETED_SENTINEL_VAL) skip = true;
        }
        if (!skip) {
            char nbuf[256];
            int nlen = len < 255 ? len : 255;
            memcpy(nbuf, s, nlen);
            nbuf[nlen] = '\0';
            Item key_item = (Item){.item = s2it(heap_create_name(nbuf, nlen))};
            array_push(arr, key_item);
        }
        e = e->next;
    }
    // Phase-5D: legacy __get_<name>/__set_<name> pass-2 scan removed.
    // Accessor properties now use IS_ACCESSOR shape flag with bare-name
    // shape entries — pass 1 above already enumerates them.
    // v26: for prototype Maps with __class_name__ and __is_proto__, append builtin method names
    {
        bool ip_own = false;
        js_map_get_fast_ext(m, "__is_proto__", 12, &ip_own);
        if (ip_own) {
            JsClass cls = js_class_id((Item){.map = m});
            if (cls != JS_CLASS_NONE) {
                    TypeId lookup_type = LMD_TYPE_MAP;
                    if (cls == JS_CLASS_STRING) lookup_type = LMD_TYPE_STRING;
                    else if (cls == JS_CLASS_ARRAY) lookup_type = LMD_TYPE_ARRAY;
                    else if (cls == JS_CLASS_NUMBER) lookup_type = LMD_TYPE_INT;
                    else if (cls == JS_CLASS_FUNCTION) lookup_type = LMD_TYPE_FUNC;
                    // Append builtin method names (they are "own" properties of the prototype)
                    int prev_len = arr->length;
                    js_append_builtin_method_names(lookup_type, result);
                    // Deduplicate: remove any that were already in the list
                    for (int i = arr->length - 1; i >= prev_len; i--) {
                        String* new_s = it2s(arr->items[i]);
                        bool dup = false;
                        for (int j = 0; j < prev_len; j++) {
                            String* old_s = it2s(arr->items[j]);
                            if (old_s && new_s && old_s->len == new_s->len && memcmp(old_s->chars, new_s->chars, new_s->len) == 0) {
                                dup = true; break;
                            }
                        }
                        if (dup) {
                            for (int k = i; k < arr->length - 1; k++) arr->items[k] = arr->items[k+1];
                            arr->length--;
                        }
                    }
            }
        }
    }
    return result;
}

// v20: helper — check if a property name is a valid ES array index (0..2^32-2)
// Returns the numeric index, or -1 if not a valid index
static int64_t js_parse_array_index(const char* s, int len) {
    if (len <= 0 || len > 10) return -1; // max "4294967294" is 10 chars
    if (s[0] == '0' && len > 1) return -1; // no leading zeros except "0"
    int64_t val = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        val = val * 10 + (s[i] - '0');
        if (val > 4294967294LL) return -1; // 2^32 - 2
    }
    return val;
}

// v20: comparison function for sorting index keys
static int js_index_entry_cmp(const void* a, const void* b) {
    int64_t ia = *(const int64_t*)a;
    int64_t ib = *(const int64_t*)b;
    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
}

extern "C" Item js_object_keys(Item object) {
    // Proxy [[OwnKeys]] trap — returns enumerable string keys
    if (js_is_proxy(object)) {
        Item all_keys = js_proxy_trap_own_keys(object);
        if (js_check_exception()) return js_array_new(0);
        // Sort keys per OrdinaryOwnPropertyKeys: numeric indices first (ascending), then strings in order
        if (get_type_id(all_keys) != LMD_TYPE_ARRAY) return all_keys;
        Array* src = all_keys.array;
        int total = src->length;
        if (total <= 1) return all_keys;

        int idx_cap = total;
        int64_t* idx_vals = (int64_t*)alloca(idx_cap * sizeof(int64_t));
        Item* idx_items = (Item*)alloca(idx_cap * sizeof(Item));
        int idx_count = 0;
        Item* str_items = (Item*)alloca(total * sizeof(Item));
        int str_count = 0;

        for (int i = 0; i < total; i++) {
            Item k = src->items[i];
            if (get_type_id(k) != LMD_TYPE_STRING) { str_items[str_count++] = k; continue; }
            String* ks = it2s(k);
            if (!ks) { str_items[str_count++] = k; continue; }
            int64_t idx = js_parse_array_index(ks->chars, (int)ks->len);
            if (idx >= 0) {
                idx_vals[idx_count] = idx;
                idx_items[idx_count] = k;
                idx_count++;
            } else {
                str_items[str_count++] = k;
            }
        }
        // Sort index keys numerically
        for (int i = 1; i < idx_count; i++) {
            int64_t iv = idx_vals[i];
            Item ii = idx_items[i];
            int j = i - 1;
            while (j >= 0 && idx_vals[j] > iv) {
                idx_vals[j + 1] = idx_vals[j];
                idx_items[j + 1] = idx_items[j];
                j--;
            }
            idx_vals[j + 1] = iv;
            idx_items[j + 1] = ii;
        }
        Item result = js_array_new(0);
        for (int i = 0; i < idx_count; i++) js_array_push(result, idx_items[i]);
        for (int i = 0; i < str_count; i++) js_array_push(result, str_items[i]);
        return result;
    }
    // ES6: ToObject for primitives
    TypeId ot = get_type_id(object);
    if (ot == LMD_TYPE_STRING) {
        String* str = it2s(object);
        int slen = str ? (int)str->len : 0;
        Item result = js_array_new(slen);
        for (int i = 0; i < slen; i++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", i);
            js_array_set(result, (Item){.item = i2it(i)}, (Item){.item = s2it(heap_create_name(buf, strlen(buf)))});
        }
        return result;
    }
    if (ot == LMD_TYPE_INT || ot == LMD_TYPE_FLOAT || ot == LMD_TYPE_BOOL) {
        return js_array_new(0);
    }
    if (!js_require_object_type(object, "keys")) return js_array_new(0);
    TypeId type = get_type_id(object);

    // For arrays, return indices as string keys: ["0", "1", "2", ...]
    if (type == LMD_TYPE_ARRAY) {
        int len = object.array->length;
        Item result = js_array_new(0);
        Map* pm = js_array_props_map(object.array);
        for (int i = 0; i < len; i++) {
            // v25: skip deleted elements (holes)... unless an accessor descriptor
            // is registered for this index in the companion map (Object.defineProperty
            // on an array index installs the data slot as a hole and stores get/set in pm).
            if (object.array->items[i].item == JS_DELETED_SENTINEL_VAL) {
                bool has_accessor = false;
                if (pm) {
                    // AT-3: IS_ACCESSOR shape-flag probe under digit-string name
                    // (post-AT-1 the intercept routes accessor writes here).
                    char idx_buf[32];
                    int idx_len = snprintf(idx_buf, sizeof(idx_buf), "%d", i);
                    Item pm_item = (Item){.map = pm};
                    ShapeEntry* _se_idx = js_find_shape_entry(pm_item, idx_buf, idx_len);
                    if (_se_idx && jspd_is_accessor(_se_idx)) {
                        bool slot_found = false;
                        Item slot_val = js_map_get_fast_ext(pm, idx_buf, idx_len, &slot_found);
                        has_accessor = slot_found && slot_val.item != JS_DELETED_SENTINEL_VAL;
                    }
                }
                if (!has_accessor) continue;
            }
            // v27: skip non-enumerable elements (defineProperty with enumerable: false)
            if (pm) {
                char ne_buf[32];
                snprintf(ne_buf, sizeof(ne_buf), "__ne_%d", i);
                bool ne_found = false;
                Item ne_val = js_map_get_fast_ext(pm, ne_buf, (int)strlen(ne_buf), &ne_found);
                if (ne_found && js_is_truthy(ne_val)) continue;
                // AT-1: accessor descriptors with enumerable=false store the bit
                // directly on the digit-string shape entry (not as __ne_<idx> marker).
                char idx_buf[32];
                int idx_len = snprintf(idx_buf, sizeof(idx_buf), "%d", i);
                Item pm_item = (Item){.map = pm};
                ShapeEntry* _se_idx2 = js_find_shape_entry(pm_item, idx_buf, idx_len);
                if (_se_idx2 && !jspd_is_enumerable(_se_idx2)) continue;
            }
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", i);
            Item key_str = (Item){.item = s2it(heap_create_name(buf))};
            js_array_push(result, key_str);
        }
        // v25: also include custom (non-index) properties from companion map
        if (pm && pm->type) {
            TypeMap* pmt = (TypeMap*)pm->type;
            ShapeEntry* e = pmt->shape;
            while (e) {
                const char* s = e->name->str;
                int slen = (int)e->name->length;
                // skip internal markers (__xx_xxx), deleted sentinels, non-enumerable
                if (slen >= 2 && s[0] == '_' && s[1] == '_') { e = e->next; continue; }
                Item val = _map_read_field(e, pm->data);
                if (val.item == JS_DELETED_SENTINEL_VAL) { e = e->next; continue; }
                // skip non-enumerable (Stage A3: shape-flag-first)
                if (!js_props_query_enumerable(pm, e, s, slen)) { e = e->next; continue; }
                Item key_item = (Item){.item = s2it(heap_create_name(s, slen))};
                js_array_push(result, key_item);
                e = e->next;
            }
        }
        return result;
    }

    // Functions: return enumerable properties from properties_map
    if (type == LMD_TYPE_FUNC) {
        Item result = js_array_new(0);
        JsFuncProps* fn_props = (JsFuncProps*)object.function;
        if (fn_props->properties_map.item != 0 && get_type_id(fn_props->properties_map) == LMD_TYPE_MAP) {
            Map* pm = fn_props->properties_map.map;
            if (pm && pm->type) {
                TypeMap* pmt = (TypeMap*)pm->type;
                ShapeEntry* e = pmt->shape;
                while (e) {
                    const char* s = e->name->str;
                    int slen = (int)e->name->length;
                    // skip internal markers (__ne_, __nw_, __nc_, etc.)
                    if (slen >= 2 && s[0] == '_' && s[1] == '_') { e = e->next; continue; }
                    // skip deleted sentinels
                    Item val = _map_read_field(e, pm->data);
                    if (val.item == JS_DELETED_SENTINEL_VAL) { e = e->next; continue; }
                    // skip non-enumerable properties (Stage A3: shape-flag-first)
                    if (!js_props_query_enumerable(pm, e, s, slen)) { e = e->next; continue; }
                    js_array_push(result, (Item){.item = s2it(heap_create_name(s, slen))});
                    e = e->next;
                }
            }
        }
        return result;
    }

    if (type != LMD_TYPE_MAP) {
        return js_array_new(0);
    }

    Map* m = object.map;
    if (!m || !m->type) return js_array_new(0);

    // v25: String wrapper objects — enumerate character indices then non-internal properties
    {
        if (js_class_id((Item){.map = m}) == JS_CLASS_STRING) {
                bool own_pv = false;
                Item pv = js_map_get_fast_ext(m, "__primitiveValue__", 18, &own_pv);
                if (own_pv && get_type_id(pv) == LMD_TYPE_STRING) {
                    String* pv_s = it2s(pv);
                    int slen = pv_s ? (int)pv_s->len : 0;
                    Item result = js_array_new(slen);
                    for (int i = 0; i < slen; i++) {
                        char buf[16];
                        int blen = snprintf(buf, sizeof(buf), "%d", i);
                        js_array_set(result, (Item){.item = i2it(i)}, (Item){.item = s2it(heap_create_name(buf, blen))});
                    }
                    return result;
                }
        }
    }

    TypeMap* tm = (TypeMap*)m->type;
    Item result = js_array_new(0);
    Array* arr = result.array;

    // v20: ES spec property enumeration order:
    //   1. Integer indices in ascending numeric order
    //   2. Non-index strings in creation order
    // We collect into two separate arrays, then merge.

    // Temporary storage for index keys: pairs of (numeric_index, Item_string)
    int idx_cap = 16, idx_count = 0;
    int64_t* idx_vals = (int64_t*)alloca(idx_cap * sizeof(int64_t));
    Item* idx_items = (Item*)alloca(idx_cap * sizeof(Item));

    // Non-index keys in insertion order
    int str_cap = 16, str_count = 0;
    Item* str_items = (Item*)alloca(str_cap * sizeof(Item));

    // Main pass: collect enumerable own properties
    ShapeEntry* e = tm->shape;
    while (e) {
        const char* s = e->name->str;
        int len = (int)e->name->length;
        bool skip = false;
        if (len >= 2 && s[0] == '_' && s[1] == '_') {
            skip = true;
        }
        if (!skip) {
            Item val = _map_read_field(e, m->data);
            if (val.item == JS_DELETED_SENTINEL_VAL) { e = e->next; continue; }
            // skip non-enumerable properties (Stage A3: shape-flag-first)
            if (!js_props_query_enumerable(m, e, s, len)) { e = e->next; continue; }
            char nbuf[256];
            int nlen = len < 255 ? len : 255;
            memcpy(nbuf, s, nlen);
            nbuf[nlen] = '\0';
            Item key_str = (Item){.item = s2it(heap_create_name(nbuf, nlen))};

            // v20: classify as index vs string key
            int64_t idx = js_parse_array_index(s, len);
            if (idx >= 0) {
                if (idx_count >= idx_cap) {
                    // overflow alloca - just append to string keys as fallback
                    if (str_count < str_cap) str_items[str_count++] = key_str;
                    else array_push(arr, key_str);
                } else {
                    idx_vals[idx_count] = idx;
                    idx_items[idx_count] = key_str;
                    idx_count++;
                }
            } else {
                if (str_count < str_cap) str_items[str_count++] = key_str;
                else array_push(arr, key_str);
            }
        }
        e = e->next;
    }

    // Phase-5D: legacy __get_<name>/__set_<name> pass-2 scan removed.
    // Accessor properties use IS_ACCESSOR shape flag with bare-name shape
    // entries — pass 1 above already enumerates them.

    // v20: sort index keys numerically
    // Simple insertion sort (typically few index keys on objects)
    for (int i = 1; i < idx_count; i++) {
        int64_t iv = idx_vals[i];
        Item ii = idx_items[i];
        int j = i - 1;
        while (j >= 0 && idx_vals[j] > iv) {
            idx_vals[j + 1] = idx_vals[j];
            idx_items[j + 1] = idx_items[j];
            j--;
        }
        idx_vals[j + 1] = iv;
        idx_items[j + 1] = ii;
    }

    // Build final result: index keys first, then string keys, then overflow
    Item final_result = js_array_new(idx_count + str_count + arr->length);
    Array* final_arr = final_result.array;
    final_arr->length = 0; // reset - we'll push

    for (int i = 0; i < idx_count; i++) array_push(final_arr, idx_items[i]);
    for (int i = 0; i < str_count; i++) array_push(final_arr, str_items[i]);
    for (int i = 0; i < arr->length; i++) array_push(final_arr, arr->items[i]);

    return final_result;
}

// =============================================================================
// js_to_string_val — convert any value to string (returns Item)
// =============================================================================

// v17: for-in enumeration — walks prototype chain to collect all enumerable string keys
extern "C" Item js_for_in_keys(Item object) {
    TypeId type = get_type_id(object);

    // for-in over null/undefined: 0 iterations
    if (object.item == ItemNull.item || type == LMD_TYPE_UNDEFINED) {
        return js_array_new(0);
    }

    // for arrays: return indices as string keys (own only, same as before)
    if (type == LMD_TYPE_ARRAY) {
        return js_object_keys(object);
    }

    // for functions: return enumerable own properties from properties_map
    if (type == LMD_TYPE_FUNC) {
        return js_object_keys(object);
    }

    // for non-map primitives (string, number, bool): coerce
    if (type == LMD_TYPE_STRING) {
        // enumerate string indices "0", "1", ... "length-1"
        String* s = it2s(object);
        int len = (int)s->len;
        Item result = js_array_new(len);
        for (int i = 0; i < len; i++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", i);
            Item key_str = (Item){.item = s2it(heap_create_name(buf))};
            js_array_set(result, (Item){.item = i2it(i)}, key_str);
        }
        return result;
    }

    if (type != LMD_TYPE_MAP) {
        return js_array_new(0);
    }

    // Proxy: forward for-in to target (enumerate own + inherited enumerable keys)
    if (js_is_proxy(object)) {
        return js_for_in_keys(js_proxy_get_target(object));
    }

    // walk prototype chain collecting enumerable string keys
    // use a simple seen-set via hashmap to deduplicate
    struct SeenEntry { char name[256]; };
    HashMap* seen = hashmap_new(sizeof(struct SeenEntry), 64, 0, 0,
        [](const void* item, uint64_t s0, uint64_t s1) -> uint64_t {
            return hashmap_sip(((const struct SeenEntry*)item)->name,
                strlen(((const struct SeenEntry*)item)->name), s0, s1);
        },
        [](const void* a, const void* b, void*) -> int {
            return strcmp(((const struct SeenEntry*)a)->name,
                          ((const struct SeenEntry*)b)->name);
        },
        NULL, NULL);

    // v20: separate index keys and string keys for spec-compliant ordering
    int idx_cap = 16, idx_count = 0;
    int64_t* idx_vals = (int64_t*)alloca(idx_cap * sizeof(int64_t));
    Item* idx_items = (Item*)alloca(idx_cap * sizeof(Item));

    Item str_result = js_array_new(0); // non-index string keys in creation order

    Item current = object;
    int depth = 0;
    while (current.item != ItemNull.item && get_type_id(current) == LMD_TYPE_MAP && depth < 64) {
        Map* m = current.map;
        if (m && m->type) {
            TypeMap* tm = (TypeMap*)m->type;
            ShapeEntry* e = tm->shape;
            while (e) {
                const char* s = e->name->str;
                int len = (int)e->name->length;

                // skip engine-internal properties (double-underscore prefix).
                // 'constructor' is not unconditionally skipped here — its
                // enumerability is determined by the __ne_ marker below
                // (default-set non-enumerable on class/object prototypes;
                //  user-defined static class fields override to enumerable).
                bool skip = false;
                if (len >= 2 && s[0] == '_' && s[1] == '_') {
                    skip = true;
                }

                if (!skip) {
                    // skip deleted properties
                    Item val = _map_read_field(e, m->data);
                    if (val.item == JS_DELETED_SENTINEL_VAL) skip = true;
                }

                // skip non-enumerable properties (Stage A3: shape-flag-first)
                if (!skip && !js_props_query_enumerable(m, e, s, len)) skip = true;

                if (!skip) {
                    // deduplicate: only add if not seen before
                    struct SeenEntry probe;
                    int nlen = len < 255 ? len : 255;
                    memcpy(probe.name, s, nlen);
                    probe.name[nlen] = '\0';
                    const struct SeenEntry* existing = (const struct SeenEntry*)hashmap_get(seen, &probe);
                    if (!existing) {
                        hashmap_set(seen, &probe);
                        Item key_str = (Item){.item = s2it(heap_create_name(probe.name))};
                        // v20: classify as index or string key
                        int64_t idx = js_parse_array_index(s, len);
                        if (idx >= 0 && idx_count < idx_cap) {
                            idx_vals[idx_count] = idx;
                            idx_items[idx_count] = key_str;
                            idx_count++;
                        } else {
                            js_array_push(str_result, key_str);
                        }
                    }
                }
                e = e->next;
            }
        }
        // Phase-5D: legacy __get_<name>/__set_<name> pass-2 scan removed.
        // Accessor properties use IS_ACCESSOR shape flag with bare-name
        // shape entries — pass 1 above already enumerates them.

        // walk up prototype chain
        current = js_get_prototype(current);
        depth++;
    }

    hashmap_free(seen);

    // v20: sort index keys numerically (insertion sort, typically few)
    for (int i = 1; i < idx_count; i++) {
        int64_t iv = idx_vals[i];
        Item ii = idx_items[i];
        int j = i - 1;
        while (j >= 0 && idx_vals[j] > iv) {
            idx_vals[j + 1] = idx_vals[j];
            idx_items[j + 1] = idx_items[j];
            j--;
        }
        idx_vals[j + 1] = iv;
        idx_items[j + 1] = ii;
    }

    // Build final result: index keys first, then string keys
    Array* str_arr = str_result.array;
    Item result = js_array_new(idx_count + str_arr->length);
    Array* arr = result.array;
    arr->length = 0;
    for (int i = 0; i < idx_count; i++) array_push(arr, idx_items[i]);
    for (int i = 0; i < str_arr->length; i++) array_push(arr, str_arr->items[i]);

    return result;
}

extern "C" Item js_object_get_own_property_symbols(Item object) {
    // Returns an array of all own Symbol keys of an object.
    // In our engine, symbols are stored as string keys "__sym_<N>" in the shape.
    // Walk shape entries, find __sym_* keys, convert back to symbol items.
    if (get_type_id(object) != LMD_TYPE_MAP) return js_array_new(0);
    Map* m = object.map;
    if (!m || !m->type) return js_array_new(0);
    TypeMap* tm = (TypeMap*)m->type;

    // first pass: count __sym_* entries
    int count = 0;
    ShapeEntry* e = tm->shape;
    while (e) {
        const char* s = e->name->str;
        int len = (int)e->name->length;
        if (len > 6 && strncmp(s, "__sym_", 6) == 0) {
            count++;
        }
        e = e->next;
    }

    Item result_arr = js_array_new(count);
    e = tm->shape;
    int i = 0;
    while (e) {
        const char* s = e->name->str;
        int len = (int)e->name->length;
        if (len > 6 && strncmp(s, "__sym_", 6) == 0) {
            // parse the numeric id from "__sym_<N>"
            long long id = atoll(s + 6);
            // convert back to symbol: symbol value = -(id + JS_SYMBOL_BASE)
            int64_t sym_val = -(id + (int64_t)JS_SYMBOL_BASE);
            js_array_set(result_arr, (Item){.item = i2it(i)}, (Item){.item = i2it(sym_val)});
            i++;
        }
        e = e->next;
    }
    return result_arr;
}

extern "C" Item js_to_string_val(Item value) {
    // String(Symbol()) is allowed — explicit conversion (ES spec 19.1.1)
    if (get_type_id(value) == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE) {
        return js_symbol_to_string(value);
    }
    return js_to_string(value);
}

// =============================================================================
// Number property access — MAX_SAFE_INTEGER, MIN_SAFE_INTEGER, etc.
// =============================================================================

static Item make_double(double val) {
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = val;
    return (Item){.item = d2it(fp)};
}

extern "C" Item js_number_property(Item prop_name) {
    TypeId type = get_type_id(prop_name);
    if (type != LMD_TYPE_STRING) return ItemNull;

    String* s = it2s(prop_name);
    if (!s) return ItemNull;

    if (s->len == 16 && strncmp(s->chars, "MAX_SAFE_INTEGER", 16) == 0) return make_double(9007199254740991.0);
    if (s->len == 16 && strncmp(s->chars, "MIN_SAFE_INTEGER", 16) == 0) return make_double(-9007199254740991.0);
    if (s->len == 9 && strncmp(s->chars, "MAX_VALUE", 9) == 0) return make_double(1.7976931348623157e+308);
    if (s->len == 9 && strncmp(s->chars, "MIN_VALUE", 9) == 0) return make_double(5e-324);
    if (s->len == 17 && strncmp(s->chars, "POSITIVE_INFINITY", 17) == 0) return make_double(1.0/0.0);
    if (s->len == 17 && strncmp(s->chars, "NEGATIVE_INFINITY", 17) == 0) return make_double(-1.0/0.0);
    if (s->len == 3 && strncmp(s->chars, "NaN", 3) == 0) return make_double(0.0/0.0);
    if (s->len == 7 && strncmp(s->chars, "EPSILON", 7) == 0) return make_double(2.220446049250313e-16);

    // v18k: Fall through to constructor property access for static methods
    // (isInteger, isFinite, isNaN, isSafeInteger, parseInt, parseFloat)
    Item ctor_name = (Item){.item = s2it(heap_create_name("Number", 6))};
    Item ctor = js_get_constructor(ctor_name);
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        return js_property_get(ctor, prop_name);
    }

    return ItemNull;
}

// =============================================================================
// Object.values — return array of property values
// =============================================================================

extern "C" Item js_object_values(Item object) {
    TypeId type = get_type_id(object);
    if (type == LMD_TYPE_NULL || object.item == ITEM_JS_UNDEFINED) {
        js_throw_type_error("Cannot convert undefined or null to object");
        return js_array_new(0);
    }
    if (type == LMD_TYPE_STRING) {
        String* str = it2s(object);
        int slen = str ? (int)str->len : 0;
        Item result = js_array_new(slen);
        for (int i = 0; i < slen; i++) {
            js_array_set(result, (Item){.item = i2it(i)}, (Item){.item = s2it(heap_create_name(str->chars + i, 1))});
        }
        return result;
    }
    if (type != LMD_TYPE_MAP) return js_array_new(0);

    // ES §7.3.22 EnumerableOwnPropertyNames: snapshot OwnKeys, then for each key
    // re-check enumerable via [[GetOwnProperty]] before reading the value.
    Item keys = js_object_keys(object);
    int len = (int)js_array_length(keys);
    Item result = js_array_new(0);
    for (int i = 0; i < len; i++) {
        Item key = js_array_get(keys, (Item){.item = i2it(i)});
        Item desc = js_object_get_own_property_descriptor(object, key);
        if (js_check_exception()) return result;
        if (get_type_id(desc) != LMD_TYPE_MAP) continue;
        bool en_found = false;
        Item en = js_map_get_fast_ext(desc.map, "enumerable", 10, &en_found);
        if (!en_found || !js_is_truthy(en)) continue;
        Item val = js_property_access(object, key);
        if (js_check_exception()) return result;
        js_array_push(result, val);
    }
    return result;
}

// =============================================================================
// Object.entries — return array of [key, value] pairs
// =============================================================================

extern "C" Item js_object_entries(Item object) {
    TypeId type = get_type_id(object);
    if (type == LMD_TYPE_NULL || object.item == ITEM_JS_UNDEFINED) {
        js_throw_type_error("Cannot convert undefined or null to object");
        return js_array_new(0);
    }
    if (type == LMD_TYPE_STRING) {
        String* str = it2s(object);
        int slen = str ? (int)str->len : 0;
        Item result = js_array_new(0);
        for (int i = 0; i < slen; i++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", i);
            Item pair = js_array_new(2);
            js_array_set(pair, (Item){.item = i2it(0)}, (Item){.item = s2it(heap_create_name(buf, strlen(buf)))});
            js_array_set(pair, (Item){.item = i2it(1)}, (Item){.item = s2it(heap_create_name(str->chars + i, 1))});
            js_array_push(result, pair);
        }
        return result;
    }
    if (type != LMD_TYPE_MAP) return js_array_new(0);

    // ES §7.3.22 EnumerableOwnPropertyNames: snapshot OwnKeys, re-check
    // enumerable via [[GetOwnProperty]] for each key before reading value.
    Item keys = js_object_keys(object);
    int len = (int)js_array_length(keys);
    Item result = js_array_new(0);
    for (int i = 0; i < len; i++) {
        Item key = js_array_get(keys, (Item){.item = i2it(i)});
        Item desc = js_object_get_own_property_descriptor(object, key);
        if (js_check_exception()) return result;
        if (get_type_id(desc) != LMD_TYPE_MAP) continue;
        bool en_found = false;
        Item en = js_map_get_fast_ext(desc.map, "enumerable", 10, &en_found);
        if (!en_found || !js_is_truthy(en)) continue;
        Item val = js_property_access(object, key);
        if (js_check_exception()) return result;
        Item pair = js_array_new(2);
        js_array_set(pair, (Item){.item = i2it(0)}, key);
        js_array_set(pair, (Item){.item = i2it(1)}, val);
        js_array_push(result, pair);
    }
    return result;
}

// =============================================================================
// Object.fromEntries(iterable) — create object from [key, value] pairs
// =============================================================================

extern "C" Item js_iterable_to_array(Item iterable);

extern "C" Item js_object_from_entries(Item iterable) {
    TypeId type = get_type_id(iterable);
    // Convert non-array iterables (Map, generator, etc.) to array first
    Item arr = iterable;
    if (type != LMD_TYPE_ARRAY) {
        arr = js_iterable_to_array(iterable);
        if (js_check_exception()) return ItemNull; // propagate iterator errors
        if (get_type_id(arr) != LMD_TYPE_ARRAY) return js_new_object();
    }

    Item result = js_new_object();
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        Item pair = js_array_get(arr, (Item){.item = i2it(i)});
        TypeId ptid = get_type_id(pair);
        // Spec: entry must be an object. Accept arrays, maps (including string/number wrappers).
        // Primitive strings are NOT objects — they should throw TypeError.
        if (ptid != LMD_TYPE_ARRAY && ptid != LMD_TYPE_MAP) {
            extern Item js_new_error_with_name(Item type_name, Item message);
            extern void js_throw_value(Item error);
            Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
            Item msg = (Item){.item = s2it(heap_create_name("Iterator value is not an entry object"))};
            js_throw_value(js_new_error_with_name(tn, msg));
            return ItemNull;
        }
        Item key, val;
        if (ptid == LMD_TYPE_ARRAY) {
            key = js_array_get(pair, (Item){.item = i2it(0)});
            val = js_array_get(pair, (Item){.item = i2it(1)});
        } else {
            // MAP or String: access [0] and [1] via property_get with string keys
            Item k0 = (Item){.item = s2it(heap_create_name("0", 1))};
            Item k1 = (Item){.item = s2it(heap_create_name("1", 1))};
            key = js_property_get(pair, k0);
            val = js_property_get(pair, k1);
        }
        // Use ToPropertyKey: preserves Symbol keys, converts others to string
        extern Item js_to_property_key(Item key);
        Item prop_key = js_to_property_key(key);
        js_property_set(result, prop_key, val);
    }
    return result;
}

// =============================================================================
// Object.groupBy(items, callbackFn) — groups items into plain object by key
// =============================================================================

extern "C" Item js_iterable_to_array(Item iterable);

extern "C" Item js_object_group_by(Item items, Item callback) {
    extern Item js_throw_type_error(const char* msg);
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        js_throw_type_error("groupBy callback is not a function");
        return ItemNull;
    }
    // Convert iterable to array first
    Item arr = js_iterable_to_array(items);
    // Create null-prototype object per spec
    Item result = js_object_create(ItemNull);
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        Item elem = js_array_get(arr, (Item){.item = i2it(i)});
        Item idx_item = {.item = i2it(i)};
        Item fn_args[2] = {elem, idx_item};
        Item key = js_call_function(callback, make_js_undefined(), fn_args, 2);
        // Stage A1: ToPropertyKey per spec — Symbol callback returns must yield
        // a property key (__sym_N), not throw via js_to_string.
        Item key_str = js_to_property_key(key);
        // get or create array for this group
        String* ks = it2s(key_str);
        bool found = false;
        Item group = js_map_get_fast_ext(result.map, ks->chars, (int)ks->len, &found);
        if (!found) {
            group = js_array_new(0);
            js_property_set(result, key_str, group);
        }
        js_array_push(group, elem);
    }
    return result;
}

// =============================================================================
// Map.groupBy(items, callbackFn) — groups items into a Map by key
// =============================================================================

extern "C" Item js_map_collection_new(void);

extern "C" Item js_map_group_by(Item items, Item callback) {
    extern Item js_collection_method(Item obj, int method_id, Item arg1, Item arg2);
    extern Item js_throw_type_error(const char* msg);
    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        js_throw_type_error("Map.groupBy callback is not a function");
        return ItemNull;
    }
    Item result = js_map_collection_new();
    // Convert iterable to array first
    Item arr = js_iterable_to_array(items);
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        Item elem = js_array_get(arr, (Item){.item = i2it(i)});
        Item idx_item = {.item = i2it(i)};
        Item fn_args[2] = {elem, idx_item};
        Item key = js_call_function(callback, make_js_undefined(), fn_args, 2);
        // has(key) -> method_id=2
        Item has = js_collection_method(result, 2, key, ItemNull);
        if (it2b(has)) {
            // get(key) -> method_id=1, then push elem
            Item group = js_collection_method(result, 1, key, ItemNull);
            js_array_push(group, elem);
        } else {
            Item group = js_array_new(0);
            js_array_push(group, elem);
            // set(key, group) -> method_id=0
            js_collection_method(result, 0, key, group);
        }
    }
    return result;
}

// =============================================================================
// Object.is(value1, value2) — SameValue comparison (handles NaN, +0/-0)
// =============================================================================

extern "C" Item js_object_is(Item left, Item right) {
    TypeId left_type = get_type_id(left);
    TypeId right_type = get_type_id(right);

    bool left_is_num = (left_type == LMD_TYPE_INT || left_type == LMD_TYPE_INT64 || left_type == LMD_TYPE_FLOAT);
    bool right_is_num = (right_type == LMD_TYPE_INT || right_type == LMD_TYPE_INT64 || right_type == LMD_TYPE_FLOAT);
    if (left_is_num && right_is_num) {
        double l = (left_type == LMD_TYPE_FLOAT) ? it2d(left) : (double)it2i(left);
        double r = (right_type == LMD_TYPE_FLOAT) ? it2d(right) : (double)it2i(right);
        // Object.is(NaN, NaN) → true (unlike ===)
        if (isnan(l) && isnan(r)) return (Item){.item = b2it(true)};
        if (isnan(l) || isnan(r)) return (Item){.item = b2it(false)};
        // Object.is(+0, -0) → false (unlike ===)
        if (l == 0.0 && r == 0.0) {
            return (Item){.item = b2it(signbit(l) == signbit(r))};
        }
        return (Item){.item = b2it(l == r)};
    }

    // Fall back to strict equality for non-numeric types
    return js_strict_equal(left, right);
}

// =============================================================================
// Native assert.sameValue / assert.notSameValue for test262 (debug builds only)
// =============================================================================
// These bypass the JS-level assert.sameValue/notSameValue, avoiding:
//   - full JS function dispatch overhead (property lookup, args array, etc.)
//   - string concatenation for error messages on the hot (passing) path
// The transpiler intercepts assert.sameValue(a,b,msg) calls and emits direct
// calls to these C++ functions instead.

#ifndef NDEBUG

// helper: build error message string for assert.sameValue/notSameValue
static Item assert_build_error_msg(Item actual, Item expected, Item message, bool same) {
    extern Item js_to_string_val(Item value);

    Item actual_str = js_to_string_val(actual);
    Item expected_str = js_to_string_val(expected);
    String* a_s = it2s(actual_str);
    String* e_s = it2s(expected_str);
    const char* a_chars = a_s ? a_s->chars : "undefined";
    int a_len = a_s ? (int)a_s->len : 9;
    const char* e_chars = e_s ? e_s->chars : "undefined";
    int e_len = e_s ? (int)e_s->len : 9;

    // format: "[<message> ]Expected SameValue(«<actual>», «<expected>») to be true/false"
    const char* tail = same ? "\xC2\xBB) to be true" : "\xC2\xBB) to be false";
    const char* mid = "\xC2\xBB, \xC2\xAB";
    const char* head = "Expected SameValue(\xC2\xAB";

    // get optional message prefix
    const char* msg_chars = NULL;
    int msg_len = 0;
    if (get_type_id(message) == LMD_TYPE_STRING) {
        String* m_s = it2s(message);
        if (m_s && m_s->len > 0) { msg_chars = m_s->chars; msg_len = (int)m_s->len; }
    }

    int total = msg_len + (msg_len > 0 ? 1 : 0) + (int)strlen(head) + a_len +
                (int)strlen(mid) + e_len + (int)strlen(tail) + 1;
    char* buf = (char*)mem_alloc(total, MEM_CAT_JS_RUNTIME);
    int pos = 0;
    if (msg_chars) {
        memcpy(buf + pos, msg_chars, msg_len); pos += msg_len;
        buf[pos++] = ' ';
    }
    int hlen = (int)strlen(head);
    memcpy(buf + pos, head, hlen); pos += hlen;
    memcpy(buf + pos, a_chars, a_len); pos += a_len;
    int mlen = (int)strlen(mid);
    memcpy(buf + pos, mid, mlen); pos += mlen;
    memcpy(buf + pos, e_chars, e_len); pos += e_len;
    int tlen = (int)strlen(tail);
    memcpy(buf + pos, tail, tlen); pos += tlen;
    buf[pos] = '\0';

    Item result = (Item){.item = s2it(heap_create_name(buf, pos))};
    mem_free(buf);
    return result;
}

extern "C" void js_assert_same_value(Item actual, Item expected, Item message) {
    // SameValue semantics: NaN === NaN, +0 !== -0
    Item result = js_object_is(actual, expected);
    if (it2b(result)) return;  // fast path: values are the same

    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);

    Item msg = assert_build_error_msg(actual, expected, message, true);
    Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
    js_throw_value(js_new_error_with_name(err_name, msg));
}

extern "C" void js_assert_not_same_value(Item actual, Item unexpected, Item message) {
    Item result = js_object_is(actual, unexpected);
    if (!it2b(result)) return;  // fast path: values are different

    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);

    Item msg = assert_build_error_msg(actual, unexpected, message, false);
    Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
    js_throw_value(js_new_error_with_name(err_name, msg));
}

// =============================================================================
// Native compareArray / assert.compareArray for test262 (debug builds only)
// =============================================================================

// check if item is an array or typed array (array-like for comparison)
static bool is_array_like(Item v) {
    TypeId t = get_type_id(v);
    if (t == LMD_TYPE_ARRAY) return true;
    if (t == LMD_TYPE_MAP && ((Container*)(uintptr_t)v.item)->map_kind == MAP_KIND_TYPED_ARRAY) return true;
    return false;
}

// get length of array or typed array (for compareArray)
static int64_t array_like_length(Item v) {
    extern int64_t js_array_length(Item array);
    TypeId t = get_type_id(v);
    if (t == LMD_TYPE_ARRAY) return js_array_length(v);
    // for typed arrays, use property access for "length"
    if (t == LMD_TYPE_MAP) {
        extern Item js_property_access(Item object, Item key);
        Item len_key = (Item){.item = s2it(heap_create_name("length"))};
        Item len_val = js_property_access(v, len_key);
        if (get_type_id(len_val) == LMD_TYPE_INT) return (int64_t)it2i(len_val);
    }
    return 0;
}

// compareArray(a, b): element-wise SameValue comparison, returns bool Item
extern "C" Item js_compare_array(Item a, Item b) {
    extern Item js_array_get_int(Item array, int64_t index);

    if (!is_array_like(a) || !is_array_like(b))
        return (Item){.item = b2it(false)};

    int64_t len_a = array_like_length(a);
    int64_t len_b = array_like_length(b);
    if (len_a != len_b) return (Item){.item = b2it(false)};

    for (int64_t i = 0; i < len_a; i++) {
        Item ai = js_array_get_int(a, i);
        Item bi = js_array_get_int(b, i);
        if (!it2b(js_object_is(ai, bi))) return (Item){.item = b2it(false)};
    }
    return (Item){.item = b2it(true)};
}

// helper: format array as "[elem1, elem2, ...]" for error messages
static Item assert_format_array(Item arr) {
    extern Item js_array_get_int(Item array, int64_t index);
    extern Item js_to_string_val(Item value);

    if (!is_array_like(arr)) {
        return (Item){.item = s2it(heap_create_name("(not an array)"))};
    }
    int64_t len = array_like_length(arr);
    // pre-pass: compute total length
    int total = 2; // "[]"
    char* strs[256];
    int slens[256];
    int maxn = len > 256 ? 256 : (int)len;
    for (int i = 0; i < maxn; i++) {
        Item elem = js_array_get_int(arr, i);
        Item s = js_to_string_val(elem);
        String* ss = it2s(s);
        strs[i] = ss ? ss->chars : (char*)"undefined";
        slens[i] = ss ? (int)ss->len : 9;
        total += slens[i] + (i > 0 ? 2 : 0); // ", " separator
    }
    char* buf = (char*)mem_alloc(total + 1, MEM_CAT_JS_RUNTIME);
    int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < maxn; i++) {
        if (i > 0) { buf[pos++] = ','; buf[pos++] = ' '; }
        memcpy(buf + pos, strs[i], slens[i]); pos += slens[i];
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    Item result = (Item){.item = s2it(heap_create_name(buf, pos))};
    mem_free(buf);
    return result;
}

// assert.compareArray(actual, expected, message): throws on mismatch
extern "C" void js_assert_compare_array(Item actual, Item expected, Item message) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);

    // null checks
    TypeId at = get_type_id(actual);
    if (at == LMD_TYPE_NULL || at == LMD_TYPE_UNDEFINED) {
        const char* msg_prefix = "Actual argument shouldn't be nullish. ";
        String* ms = (get_type_id(message) == LMD_TYPE_STRING) ? it2s(message) : NULL;
        int total = (int)strlen(msg_prefix) + (ms ? (int)ms->len : 0);
        char* buf = (char*)mem_alloc(total + 1, MEM_CAT_JS_RUNTIME);
        memcpy(buf, msg_prefix, strlen(msg_prefix));
        if (ms) memcpy(buf + strlen(msg_prefix), ms->chars, ms->len);
        buf[total] = '\0';
        Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
        Item err_msg = (Item){.item = s2it(heap_create_name(buf, total))};
        mem_free(buf);
        js_throw_value(js_new_error_with_name(err_name, err_msg));
        return;
    }

    TypeId et = get_type_id(expected);
    if (et == LMD_TYPE_NULL || et == LMD_TYPE_UNDEFINED) {
        const char* msg_prefix = "Expected argument shouldn't be nullish. ";
        String* ms = (get_type_id(message) == LMD_TYPE_STRING) ? it2s(message) : NULL;
        int total = (int)strlen(msg_prefix) + (ms ? (int)ms->len : 0);
        char* buf = (char*)mem_alloc(total + 1, MEM_CAT_JS_RUNTIME);
        memcpy(buf, msg_prefix, strlen(msg_prefix));
        if (ms) memcpy(buf + strlen(msg_prefix), ms->chars, ms->len);
        buf[total] = '\0';
        Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
        Item err_msg = (Item){.item = s2it(heap_create_name(buf, total))};
        mem_free(buf);
        js_throw_value(js_new_error_with_name(err_name, err_msg));
        return;
    }

    Item result = js_compare_array(actual, expected);
    if (it2b(result)) return; // pass

    // build error message: "Actual [...] and expected [...] should have the same contents. <message>"
    Item a_fmt = assert_format_array(actual);
    Item e_fmt = assert_format_array(expected);
    String* as = it2s(a_fmt);
    String* es = it2s(e_fmt);
    String* ms = (get_type_id(message) == LMD_TYPE_STRING) ? it2s(message) : NULL;

    const char* p1 = "Actual ";
    const char* p2 = " and expected ";
    const char* p3 = " should have the same contents. ";
    int total = (int)strlen(p1) + (as ? (int)as->len : 0) + (int)strlen(p2) +
                (es ? (int)es->len : 0) + (int)strlen(p3) + (ms ? (int)ms->len : 0);
    char* buf = (char*)mem_alloc(total + 1, MEM_CAT_JS_RUNTIME);
    int pos = 0;
    int l;
    l = (int)strlen(p1); memcpy(buf + pos, p1, l); pos += l;
    if (as) { memcpy(buf + pos, as->chars, as->len); pos += (int)as->len; }
    l = (int)strlen(p2); memcpy(buf + pos, p2, l); pos += l;
    if (es) { memcpy(buf + pos, es->chars, es->len); pos += (int)es->len; }
    l = (int)strlen(p3); memcpy(buf + pos, p3, l); pos += l;
    if (ms) { memcpy(buf + pos, ms->chars, ms->len); pos += (int)ms->len; }
    buf[pos] = '\0';

    Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
    Item err_msg = (Item){.item = s2it(heap_create_name(buf, pos))};
    mem_free(buf);
    js_throw_value(js_new_error_with_name(err_name, err_msg));
}

// =============================================================================
// Native verifyProperty for test262 (debug builds only)
// =============================================================================
// Simplified native version: checks descriptor fields against
// Object.getOwnPropertyDescriptor result. Skips destructive isWritable/
// isConfigurable/isEnumerable checks for performance.

extern "C" void js_verify_property(Item obj, Item name, Item desc, Item options) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);
    extern Item js_object_get_own_property_descriptor(Item obj, Item name);
    extern Item js_has_own_property(Item obj, Item key);
    extern Item js_property_get(Item obj, Item key);
    extern Item js_to_string_val(Item value);

    Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};

    // verifyProperty requires at least 3 arguments: obj, name, desc
    // (always true when called from transpiler)

    Item originalDesc = js_object_get_own_property_descriptor(obj, name);

    // desc === undefined → verify property doesn't exist
    if (get_type_id(desc) == LMD_TYPE_UNDEFINED) {
        if (get_type_id(originalDesc) != LMD_TYPE_UNDEFINED) {
            Item name_str = js_to_string_val(name);
            String* ns = it2s(name_str);
            char buf[256];
            int len = snprintf(buf, sizeof(buf), "obj['%.*s'] descriptor should be undefined",
                              ns ? (int)ns->len : 0, ns ? ns->chars : "");
            js_throw_value(js_new_error_with_name(err_name, (Item){.item = s2it(heap_create_name(buf, len))}));
        }
        return;
    }

    // assert(hasOwnProperty(obj, name))
    if (!it2b(js_has_own_property(obj, name))) {
        Item name_str = js_to_string_val(name);
        String* ns = it2s(name_str);
        char buf[256];
        int len = snprintf(buf, sizeof(buf), "obj should have an own property %.*s",
                          ns ? (int)ns->len : 0, ns ? ns->chars : "");
        js_throw_value(js_new_error_with_name(err_name, (Item){.item = s2it(heap_create_name(buf, len))}));
        return;
    }

    // desc must be an object
    if (get_type_id(desc) != LMD_TYPE_MAP) {
        js_throw_value(js_new_error_with_name(err_name,
            (Item){.item = s2it(heap_create_name("The desc argument should be an object or undefined"))}));
        return;
    }

    if (get_type_id(originalDesc) == LMD_TYPE_UNDEFINED) {
        // property should exist but getOwnPropertyDescriptor returned undefined
        js_throw_value(js_new_error_with_name(err_name,
            (Item){.item = s2it(heap_create_name("property descriptor is undefined but property should exist"))}));
        return;
    }

    // check each descriptor field
    Item value_key = (Item){.item = s2it(heap_create_name("value", 5))};
    Item writable_key = (Item){.item = s2it(heap_create_name("writable", 8))};
    Item enumerable_key = (Item){.item = s2it(heap_create_name("enumerable", 10))};
    Item configurable_key = (Item){.item = s2it(heap_create_name("configurable", 12))};

    // collect failure messages
    char failures[1024];
    int fpos = 0;
    failures[0] = '\0';

    auto append_failure = [&](const char* msg) {
        if (fpos > 0) {
            memcpy(failures + fpos, "; ", 2);
            fpos += 2;
        }
        int ml = (int)strlen(msg);
        if (fpos + ml < (int)sizeof(failures) - 1) {
            memcpy(failures + fpos, msg, ml);
            fpos += ml;
        }
        failures[fpos] = '\0';
    };

    if (it2b(js_has_own_property(desc, value_key))) {
        Item desc_value = js_property_get(desc, value_key);
        Item orig_value = js_property_get(originalDesc, value_key);
        if (!it2b(js_object_is(desc_value, orig_value))) {
            append_failure("descriptor value mismatch");
        }
        // also check obj[name] matches
        Item obj_value = js_property_get(obj, name);
        if (!it2b(js_object_is(desc_value, obj_value))) {
            append_failure("object value mismatch");
        }
    }

    if (it2b(js_has_own_property(desc, enumerable_key))) {
        Item desc_enum = js_property_get(desc, enumerable_key);
        Item orig_enum = js_property_get(originalDesc, enumerable_key);
        bool desc_e = it2b(desc_enum);
        bool orig_e = it2b(orig_enum);
        if (desc_e != orig_e) {
            append_failure(desc_e ? "descriptor should be enumerable" : "descriptor should not be enumerable");
        }
    }

    if (it2b(js_has_own_property(desc, writable_key))) {
        Item desc_writ = js_property_get(desc, writable_key);
        Item orig_writ = js_property_get(originalDesc, writable_key);
        bool desc_w = it2b(desc_writ);
        bool orig_w = it2b(orig_writ);
        if (desc_w != orig_w) {
            append_failure(desc_w ? "descriptor should be writable" : "descriptor should not be writable");
        }
    }

    if (it2b(js_has_own_property(desc, configurable_key))) {
        Item desc_conf = js_property_get(desc, configurable_key);
        Item orig_conf = js_property_get(originalDesc, configurable_key);
        bool desc_c = it2b(desc_conf);
        bool orig_c = it2b(orig_conf);
        if (desc_c != orig_c) {
            append_failure(desc_c ? "descriptor should be configurable" : "descriptor should not be configurable");
        }
    }

    if (fpos > 0) {
        js_throw_value(js_new_error_with_name(err_name, (Item){.item = s2it(heap_create_name(failures, fpos))}));
        return;
    }

    // options.restore: restore the original descriptor
    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item restore_key = (Item){.item = s2it(heap_create_name("restore", 7))};
        Item restore_val = js_property_get(options, restore_key);
        if (it2b(restore_val)) {
            extern Item js_object_define_property(Item obj, Item name, Item desc);
            js_object_define_property(obj, name, originalDesc);
        }
    }
}

// =============================================================================
// Native assert.deepEqual for test262 (debug builds only)
// =============================================================================

// forward declaration for recursive call
static bool js_deep_equal_compare(Item a, Item b, int depth);

static bool js_deep_equal_compare(Item a, Item b, int depth) {
    extern int64_t js_array_length(Item array);
    extern Item js_array_get_int(Item array, int64_t index);
    extern Item js_property_get(Item obj, Item key);
    extern Item js_object_keys(Item object);
    extern Item js_strict_equal(Item left, Item right);

    if (depth > 100) return false; // prevent infinite recursion

    // fast path: strict equality (same reference, same primitives)
    if (it2b(js_strict_equal(a, b))) return true;

    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);

    // null/undefined: only equal if both the same
    if (ta == LMD_TYPE_NULL || ta == LMD_TYPE_UNDEFINED ||
        tb == LMD_TYPE_NULL || tb == LMD_TYPE_UNDEFINED) {
        return ta == tb && a.item == b.item;
    }

    // NaN handling: NaN === NaN for deepEqual
    if (ta == LMD_TYPE_FLOAT && tb == LMD_TYPE_FLOAT) {
        double da = it2d(a);
        double db = it2d(b);
        if (da != da && db != db) return true; // both NaN
        return da == db;
    }

    // different primitive types
    if ((ta == LMD_TYPE_BOOL || ta == LMD_TYPE_INT || ta == LMD_TYPE_FLOAT ||
         ta == LMD_TYPE_STRING) &&
        (tb == LMD_TYPE_BOOL || tb == LMD_TYPE_INT || tb == LMD_TYPE_FLOAT ||
         tb == LMD_TYPE_STRING)) {
        // both primitives but not strict equal — not deep equal
        // (cross-type like int/float: 1 === 1.0 should have passed strict equal)
        return false;
    }

    // both arrays: element-wise deep comparison
    if (ta == LMD_TYPE_ARRAY && tb == LMD_TYPE_ARRAY) {
        int64_t len_a = js_array_length(a);
        int64_t len_b = js_array_length(b);
        if (len_a != len_b) return false;
        for (int64_t i = 0; i < len_a; i++) {
            if (!js_deep_equal_compare(js_array_get_int(a, i), js_array_get_int(b, i), depth + 1))
                return false;
        }
        return true;
    }

    // both objects/maps: structural comparison
    if (ta == LMD_TYPE_MAP && tb == LMD_TYPE_MAP) {
        Item keys_a = js_object_keys(a);
        Item keys_b = js_object_keys(b);
        int64_t len_a = js_array_length(keys_a);
        int64_t len_b = js_array_length(keys_b);
        if (len_a != len_b) return false;

        for (int64_t i = 0; i < len_a; i++) {
            Item key = js_array_get_int(keys_a, i);
            // check same key exists in b
            Item val_a = js_property_get(a, key);
            Item val_b = js_property_get(b, key);
            if (!js_deep_equal_compare(val_a, val_b, depth + 1))
                return false;
        }
        return true;
    }

    // mismatched types (array vs object, etc.)
    return false;
}

extern "C" void js_assert_deep_equal(Item actual, Item expected, Item message) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);
    extern Item js_to_string_val(Item value);

    bool equal = js_deep_equal_compare(actual, expected, 0);
    if (equal) return;

    // build error message
    Item a_str = js_to_string_val(actual);
    Item e_str = js_to_string_val(expected);
    String* as = it2s(a_str);
    String* es = it2s(e_str);
    String* ms = (get_type_id(message) == LMD_TYPE_STRING) ? it2s(message) : NULL;

    const char* p1 = "Expected ";
    const char* p2 = " to be structurally equal to ";
    const char* p3 = ". ";
    int total = (int)strlen(p1) + (as ? (int)as->len : 0) + (int)strlen(p2) +
                (es ? (int)es->len : 0) + (int)strlen(p3) + (ms ? (int)ms->len : 0);
    char* buf = (char*)mem_alloc(total + 1, MEM_CAT_JS_RUNTIME);
    int pos = 0;
    int l;
    l = (int)strlen(p1); memcpy(buf + pos, p1, l); pos += l;
    if (as) { memcpy(buf + pos, as->chars, as->len); pos += (int)as->len; }
    l = (int)strlen(p2); memcpy(buf + pos, p2, l); pos += l;
    if (es) { memcpy(buf + pos, es->chars, es->len); pos += (int)es->len; }
    l = (int)strlen(p3); memcpy(buf + pos, p3, l); pos += l;
    if (ms) { memcpy(buf + pos, ms->chars, ms->len); pos += (int)ms->len; }
    buf[pos] = '\0';

    Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
    Item err_msg = (Item){.item = s2it(heap_create_name(buf, pos))};
    mem_free(buf);
    js_throw_value(js_new_error_with_name(err_name, err_msg));
}

// =============================================================================
// Native assert.throws for test262 (debug builds only)
// assert.throws(expectedErrorConstructor, func [, message])
// =============================================================================

extern "C" void js_assert_throws(Item expected_ctor, Item func, Item message) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);
    extern Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
    extern int js_check_exception(void);
    extern Item js_clear_exception(void);
    extern Item js_to_string_val(Item value);
    extern Item js_instanceof(Item left, Item right);
    extern Item js_property_get(Item obj, Item key);

    // validate func argument
    if (get_type_id(func) != LMD_TYPE_FUNC) {
        Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
        Item err_msg  = (Item){.item = s2it(heap_create_name("assert.throws requires two arguments: the error constructor and a function to run"))};
        js_throw_value(js_new_error_with_name(err_name, err_msg));
        return;
    }

    // get message prefix
    const char* msg_chars = "";
    int msg_len = 0;
    if (get_type_id(message) == LMD_TYPE_STRING) {
        String* ms = it2s(message);
        if (ms && ms->len > 0) { msg_chars = ms->chars; msg_len = (int)ms->len; }
    }

    // call the function — expect it to throw
    js_call_function(func, make_js_undefined(), NULL, 0);

    if (js_check_exception()) {
        // good — an exception was thrown. check its type.
        Item thrown = js_clear_exception();

        // if expected_ctor is undefined/null, accept any thrown error
        // (e.g. Test262Error not defined — just verify something was thrown)
        TypeId ect = get_type_id(expected_ctor);
        if (ect == LMD_TYPE_NULL || expected_ctor.item == ITEM_JS_UNDEFINED) {
            return;  // any error is acceptable
        }

        // thrown must be an object
        TypeId tid = get_type_id(thrown);
        if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_ELEMENT) {
            char buf[1200];
            int pos = 0;
            if (msg_len > 0) { memcpy(buf, msg_chars, msg_len < 1000 ? msg_len : 1000); pos = msg_len < 1000 ? msg_len : 1000; buf[pos++] = ' '; }
            const char* t = "Thrown value was not an object!";
            int tl = (int)strlen(t);
            memcpy(buf + pos, t, tl); pos += tl;
            buf[pos] = '\0';
            Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
            Item err_msg  = (Item){.item = s2it(heap_create_name(buf, pos))};
            js_throw_value(js_new_error_with_name(err_name, err_msg));
            return;
        }

        // check: thrown instanceof expectedErrorConstructor
        Item instanceof_result = js_instanceof(thrown, expected_ctor);
        bool is_instance = (get_type_id(instanceof_result) == LMD_TYPE_BOOL && it2b(instanceof_result));

        if (!is_instance) {
            // type mismatch — build error message
            Item name_key = (Item){.item = s2it(heap_create_name("name"))};
            Item exp_name = js_property_get(expected_ctor, name_key);
            // get actual constructor name via prototype chain
            extern Item js_prototype_lookup(Item obj, Item key);
            Item ctor_key = (Item){.item = s2it(heap_create_name("constructor"))};
            Item thrown_ctor = js_prototype_lookup(thrown, ctor_key);
            Item act_name = (get_type_id(thrown_ctor) != LMD_TYPE_UNDEFINED && get_type_id(thrown_ctor) != LMD_TYPE_NULL)
                ? js_property_get(thrown_ctor, name_key) : make_js_undefined();
            String* ens = (get_type_id(exp_name) == LMD_TYPE_STRING) ? it2s(exp_name) : NULL;
            String* ans = (get_type_id(act_name) == LMD_TYPE_STRING) ? it2s(act_name) : NULL;
            const char* en = ens ? ens->chars : "?";
            int enl = ens ? (int)ens->len : 1;
            const char* an = ans ? ans->chars : "?";
            int anl = ans ? (int)ans->len : 1;

            char buf[1200];
            int pos = 0;
            if (msg_len > 0) { memcpy(buf, msg_chars, msg_len < 900 ? msg_len : 900); pos = msg_len < 900 ? msg_len : 900; buf[pos++] = ' '; }

            if (enl == anl && strncmp(en, an, enl) == 0) {
                const char* t = "Expected a ";
                int tl = (int)strlen(t);
                memcpy(buf + pos, t, tl); pos += tl;
                memcpy(buf + pos, en, enl < 100 ? enl : 100); pos += enl < 100 ? enl : 100;
                const char* t2 = " but got a different error constructor with the same name";
                int t2l = (int)strlen(t2);
                memcpy(buf + pos, t2, t2l); pos += t2l;
            } else {
                const char* t = "Expected a ";
                int tl = (int)strlen(t);
                memcpy(buf + pos, t, tl); pos += tl;
                memcpy(buf + pos, en, enl < 100 ? enl : 100); pos += enl < 100 ? enl : 100;
                const char* t2 = " but got a ";
                int t2l = (int)strlen(t2);
                memcpy(buf + pos, t2, t2l); pos += t2l;
                memcpy(buf + pos, an, anl < 100 ? anl : 100); pos += anl < 100 ? anl : 100;
            }
            buf[pos] = '\0';
            Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
            Item err_msg  = (Item){.item = s2it(heap_create_name(buf, pos))};
            js_throw_value(js_new_error_with_name(err_name, err_msg));
        }
        return;
    }

    // no exception was thrown — that's a failure
    {
        Item name_key = (Item){.item = s2it(heap_create_name("name"))};
        Item exp_name = js_property_get(expected_ctor, name_key);
        String* ens = (get_type_id(exp_name) == LMD_TYPE_STRING) ? it2s(exp_name) : NULL;
        const char* en = ens ? ens->chars : "?";
        int enl = ens ? (int)ens->len : 1;

        char buf[1200];
        int pos = 0;
        if (msg_len > 0) { memcpy(buf, msg_chars, msg_len < 900 ? msg_len : 900); pos = msg_len < 900 ? msg_len : 900; buf[pos++] = ' '; }
        const char* t = "Expected a ";
        int tl = (int)strlen(t);
        memcpy(buf + pos, t, tl); pos += tl;
        memcpy(buf + pos, en, enl < 100 ? enl : 100); pos += enl < 100 ? enl : 100;
        const char* t2 = " to be thrown but no exception was thrown at all";
        int t2l = (int)strlen(t2);
        memcpy(buf + pos, t2, t2l); pos += t2l;
        buf[pos] = '\0';
        Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
        Item err_msg  = (Item){.item = s2it(heap_create_name(buf, pos))};
        js_throw_value(js_new_error_with_name(err_name, err_msg));
    }
}

// =============================================================================
// Native assert() base function for test262 (debug builds only)
// assert(mustBeTrue [, message])
// =============================================================================

extern "C" void js_assert_base(Item must_be_true, Item message) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);
    extern Item js_to_string_val(Item value);

    // check mustBeTrue === true
    if (get_type_id(must_be_true) == LMD_TYPE_BOOL && it2b(must_be_true)) return;

    // build error message
    if (get_type_id(message) == LMD_TYPE_STRING) {
        String* ms = it2s(message);
        if (ms && ms->len > 0) {
            Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
            js_throw_value(js_new_error_with_name(err_name, message));
            return;
        }
    }

    // default message: "Expected true but got <value>"
    Item val_str = js_to_string_val(must_be_true);
    String* vs = it2s(val_str);
    const char* prefix = "Expected true but got ";
    int plen = (int)strlen(prefix);
    int vlen = vs ? (int)vs->len : 9;
    const char* vchars = vs ? vs->chars : "undefined";
    char* buf = (char*)mem_alloc(plen + vlen + 1, MEM_CAT_JS_RUNTIME);
    memcpy(buf, prefix, plen);
    memcpy(buf + plen, vchars, vlen);
    buf[plen + vlen] = '\0';
    Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
    Item err_msg  = (Item){.item = s2it(heap_create_name(buf, plen + vlen))};
    mem_free(buf);
    js_throw_value(js_new_error_with_name(err_name, err_msg));
}

// =============================================================================
// Native $DONOTEVALUATE for test262 (debug builds only)
// =============================================================================

extern "C" void js_donotevaluate(void) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);
    Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
    Item err_msg  = (Item){.item = s2it(heap_create_name("Test262: This statement should not be evaluated."))};
    js_throw_value(js_new_error_with_name(err_name, err_msg));
}

// isConstructor(fn) — test262 harness helper
// Checks if fn is a constructor by examining function flags
extern "C" Item js_is_constructor(Item fn) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);

    // per harness spec: throw Test262Error for non-function arguments
    TypeId tid = get_type_id(fn);
    if (tid != LMD_TYPE_FUNC) {
        Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
        Item err_msg  = (Item){.item = s2it(heap_create_name("isConstructor: argument must be a function"))};
        js_throw_value(js_new_error_with_name(err_name, err_msg));
        return (Item){.item = ITEM_FALSE};
    }

    JsFunctionLayout* jfn = (JsFunctionLayout*)fn.function;
    // Not constructable: builtins, arrow functions, generators
    if (jfn->builtin_id > 0 || jfn->builtin_id == -2 ||
        (jfn->flags & (JS_FUNC_FLAG_ARROW_G | JS_FUNC_FLAG_GENERATOR_G))) {
        return (Item){.item = ITEM_FALSE};
    }
    return (Item){.item = ITEM_TRUE};
}

#endif // !NDEBUG

// =============================================================================
// Object.assign(target, ...sources)
// =============================================================================

extern "C" Item js_object_assign(Item target, Item* sources, int count) {
    TypeId tid = get_type_id(target);
    if (tid == LMD_TYPE_NULL || tid == LMD_TYPE_UNDEFINED ||
        (target.item == 0 && tid != LMD_TYPE_INT)) {
        extern Item js_new_error_with_name(Item type_name, Item message);
        extern void js_throw_value(Item error);
        Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item msg = (Item){.item = s2it(heap_create_name("Cannot convert undefined or null to object"))};
        js_throw_value(js_new_error_with_name(tn, msg));
        return ItemNull;
    }
    if (tid != LMD_TYPE_MAP) {
        extern Item js_to_object(Item value);
        target = js_to_object(target);
        if (get_type_id(target) != LMD_TYPE_MAP) return target;
    }
    for (int i = 0; i < count; i++) {
        Item source = sources[i];
        TypeId stid = get_type_id(source);
        // skip null/undefined sources
        if (stid == LMD_TYPE_NULL || stid == LMD_TYPE_UNDEFINED) continue;
        // string source: enumerate characters as own properties
        if (stid == LMD_TYPE_STRING) {
            String* ss = it2s(source);
            if (ss) {
                int slen = (int)ss->len;
                for (int ci = 0; ci < slen; ci++) {
                    char idx_buf[16];
                    snprintf(idx_buf, sizeof(idx_buf), "%d", ci);
                    Item key = (Item){.item = s2it(heap_create_name(idx_buf))};
                    char ch_buf[2] = {ss->chars[ci], '\0'};
                    Item val = (Item){.item = s2it(heap_create_name(ch_buf, 1))};
                    js_property_set(target, key, val);
                }
            }
            continue;
        }
        if (stid != LMD_TYPE_MAP) continue;
        Map* m = source.map;
        if (!m || !m->type) continue;
        TypeMap* tm = (TypeMap*)m->type;
        // ES OrdinaryOwnPropertyKeys: strings before symbols. Two-pass.
        for (int pass = 0; pass < 2; pass++) {
        ShapeEntry* e = tm->shape;
        while (e) {
            if (e->name) {
                const char* n = e->name->str;
                int nlen = (int)e->name->length;
                // v20: Skip internal marker properties (but allow __sym_ symbol keys)
                bool is_sym = (nlen >= 6 && n[0] == '_' && n[1] == '_' &&
                               n[2] == 's' && n[3] == 'y' && n[4] == 'm' && n[5] == '_');
                if (nlen >= 2 && n[0] == '_' && n[1] == '_') {
                    if (!is_sym) {
                        e = e->next;
                        continue;
                    }
                }
                // pass 0: strings only; pass 1: symbols only.
                if ((pass == 0 && is_sym) || (pass == 1 && !is_sym)) {
                    e = e->next;
                    continue;
                }
                // Stage A3: shape-flag-first non-enumerable check
                if (!js_props_query_enumerable(m, e, n, nlen)) {
                    e = e->next;
                    continue;
                }
                Item key = (Item){.item = s2it(heap_create_name(n, nlen))};
                Item val;
                // Stage A1.7: route shape-iteration accessor dispatch through
                // js_ordinary_resolve_shape_value.
                {
                    JsResolveFieldStatus rs = js_ordinary_resolve_shape_value(
                        e, m, source, &val);
                    if (rs == JS_RESOLVE_DELETED) { e = e->next; continue; }
                    if (rs == JS_RESOLVE_THREW) return ItemNull;
                }
                {
                    // Per ES spec: Set with Throw=true; throw TypeError if target
                    // has a non-writable property of the same name
                    if (get_type_id(target) == LMD_TYPE_MAP) {
                        ShapeEntry* _se_t = js_find_shape_entry(target, n, nlen);
                        if (!js_props_query_writable(target.map, _se_t, n, nlen)) {
                            char emsg[256];
                            snprintf(emsg, sizeof(emsg), "Cannot assign to read only property '%.*s' of object", nlen > 200 ? 200 : nlen, n);
                            js_throw_type_error(emsg);
                            return ItemNull;
                        }
                    }
                    js_property_set(target, key, val);
                    if (js_check_exception()) return ItemNull;
                }
            }
            e = e->next;
        }
        } // end pass loop
        // Second pass: detect accessor-only properties (__get_<name> / __set_<name>)
        // that have no regular data entry (e.g. properties defined only with a getter)
        // Phase-5D: legacy __get_<name>/__set_<name> pass-2 scan removed.
        // Accessor properties use IS_ACCESSOR shape flag with bare-name
        // shape entries — pass 1 above already handled them.
    }
    return target;
}

// Object spread: copy all own enumerable properties from source into target
// Used for { ...source } in object literals
extern "C" Item js_object_spread_into(Item target, Item source) {
    if (get_type_id(target) != LMD_TYPE_MAP) return target;
    if (get_type_id(source) != LMD_TYPE_MAP) return target;
    Map* m = source.map;
    if (!m || !m->type) return target;
    TypeMap* tm = (TypeMap*)m->type;
    ShapeEntry* e = tm->shape;
    while (e) {
        if (e->name) {
            const char* n = e->name->str;
            int nlen = (int)e->name->length;
            // v20: Skip internal marker properties (__ne_, __nw_, __nc_, __get_, __set_, __proto__, __class_name__, __sym_)
            if (nlen >= 2 && n[0] == '_' && n[1] == '_') {
                e = e->next;
                continue;
            }
            // Stage A3: shape-flag-first non-enumerable check
            if (!js_props_query_enumerable(m, e, n, nlen)) {
                e = e->next;
                continue;
            }
            Item val;
            // Stage A1.7: route shape-iteration accessor dispatch through
            // js_ordinary_resolve_shape_value. CopyDataProperties resolves
            // setter-only accessors as undefined and copies them.
            {
                JsResolveFieldStatus rs = js_ordinary_resolve_shape_value(
                    e, m, source, &val);
                if (rs == JS_RESOLVE_DELETED) { e = e->next; continue; }
                if (rs == JS_RESOLVE_THREW) return target;
            }
            {
                Item key = (Item){.item = s2it(heap_create_name(n, nlen))};
                js_property_set(target, key, val);
            }
        }
        e = e->next;
    }
    // Second pass: detect accessor-only properties (__get_<name>)
    // Phase-5D: legacy __get_<name>/__set_<name> pass-2 scan removed.
    return target;
}

// =============================================================================
// Helper: check if a Map with __class_name__ has a built-in method as own prop
// Returns true if the map is a prototype object and the key matches a builtin.
// =============================================================================
static bool js_map_has_builtin_method(Map* m, const char* name, int len) {
    // Only check builtin methods on actual prototype objects (not instances)
    bool ip_own = false;
    js_map_get_fast_ext(m, "__is_proto__", 12, &ip_own);
    if (!ip_own) return false;
    JsClass cls = js_class_id((Item){.map = m});
    if (cls == JS_CLASS_NONE) return false;
    // map class to TypeId for builtin lookup
    TypeId lookup_type = LMD_TYPE_MAP;
    if (cls == JS_CLASS_STRING) lookup_type = LMD_TYPE_STRING;
    else if (cls == JS_CLASS_ARRAY) lookup_type = LMD_TYPE_ARRAY;
    else if (cls == JS_CLASS_NUMBER) lookup_type = LMD_TYPE_INT;
    else if (cls == JS_CLASS_FUNCTION) lookup_type = LMD_TYPE_FUNC;
    else if (cls == JS_CLASS_BOOLEAN) lookup_type = LMD_TYPE_BOOL;
    // Skip "constructor" — handled separately
    if (len == 11 && strncmp(name, "constructor", 11) == 0) return true;
    // Symbol.iterator (__sym_1) is a virtual property on Array and String prototypes
    if (len == 7 && strncmp(name, "__sym_1", 7) == 0) {
        if (lookup_type == LMD_TYPE_ARRAY || lookup_type == LMD_TYPE_STRING) return true;
    }
    Item builtin = js_lookup_builtin_method(lookup_type, name, len);
    if (builtin.item != ItemNull.item) return true;
    // check class-name-specific methods not in js_lookup_builtin_method
    // (Date, RegExp, Error, Map/Set collections, etc.)
    if (cls == JS_CLASS_DATE) {
        // Date prototype methods — same table as js_property_get
        static const char* date_methods[] = {
            "getTime","getFullYear","getMonth","getDate","getHours","getMinutes",
            "getSeconds","getMilliseconds","toISOString","toJSON","toUTCString",
            "toDateString","toTimeString","toString","toLocaleDateString","valueOf",
            "getDay","getUTCFullYear","getUTCMonth","getUTCDate","getUTCHours",
            "getUTCMinutes","getUTCSeconds","getUTCMilliseconds","getUTCDay",
            "getTimezoneOffset","setTime","setFullYear","setMonth","setDate",
            "setHours","setMinutes","setSeconds","setMilliseconds",
            "setUTCFullYear","setUTCMonth","setUTCDate","setUTCHours",
            "setUTCMinutes","setUTCSeconds","setUTCMilliseconds",
            "getYear","setYear","toLocaleString","toLocaleTimeString",NULL
        };
        for (int i = 0; date_methods[i]; i++) {
            if ((int)strlen(date_methods[i]) == len && strncmp(name, date_methods[i], len) == 0) return true;
        }
    } else if (cls == JS_CLASS_REGEXP) {
        static const char* regexp_methods[] = {
            "exec","test","toString","compile",NULL
        };
        for (int i = 0; regexp_methods[i]; i++) {
            if ((int)strlen(regexp_methods[i]) == len && strncmp(name, regexp_methods[i], len) == 0) return true;
        }
        // symbol methods
        if (len >= 7 && strncmp(name, "__sym_", 6) == 0) return true;
    }
    // collection methods (Map, Set, WeakMap, WeakSet)
    if (cls == JS_CLASS_MAP || cls == JS_CLASS_SET || cls == JS_CLASS_WEAK_MAP || cls == JS_CLASS_WEAK_SET) {
        static const char* collection_methods[] = {
            "values","keys","entries","forEach","has","get","set",
            "add","delete","clear",NULL
        };
        for (int i = 0; collection_methods[i]; i++) {
            if ((int)strlen(collection_methods[i]) == len && strncmp(name, collection_methods[i], len) == 0) return true;
        }
        if (len == 7 && strncmp(name, "__sym_1", 7) == 0) return true;
    }
    return false;
}

// =============================================================================
// obj.hasOwnProperty(key) / Object.hasOwn(obj, key)
// =============================================================================

extern "C" Item js_has_own_property(Item obj, Item key) {
    // Proxy: forward to getOwnPropertyDescriptor trap
    if (js_is_proxy(obj)) {
        Item desc = js_proxy_trap_get_own_property_descriptor(obj, key);
        if (js_check_exception()) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(desc.item != ItemNull.item && get_type_id(desc) != LMD_TYPE_UNDEFINED)};
    }
    // v23: handle array objects — numeric indices and "length"
    if (get_type_id(obj) == LMD_TYPE_ARRAY) {
        // Stage A1: ToPropertyKey — symbol keys may be present on companion map.
        Item k = js_to_property_key(key);
        if (get_type_id(k) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
        String* ks = it2s(k);
        if (!ks) return (Item){.item = b2it(false)};
        // "length" is always an own property of arrays
        if (ks->len == 6 && strncmp(ks->chars, "length", 6) == 0) {
            return (Item){.item = b2it(true)};
        }
        // Check if it's a valid numeric index within bounds
        // Parse as integer directly to avoid depending on static js_get_number
        bool is_numeric = (ks->len > 0 && ks->len <= 10);
        int64_t idx = 0;
        if (is_numeric) {
            for (int i = 0; i < (int)ks->len; i++) {
                char c = ks->chars[i];
                if (c < '0' || c > '9') { is_numeric = false; break; }
                idx = idx * 10 + (c - '0');
            }
            // reject leading zeros like "01" (except "0" itself)
            if (is_numeric && ks->len > 1 && ks->chars[0] == '0') is_numeric = false;
        }
        if (is_numeric) {
            Array* arr = obj.array;
            if (idx >= 0 && idx < arr->length) {
                // v25: check for deleted sentinel (array hole)
                if (arr->items[idx].item == JS_DELETED_SENTINEL_VAL) {
                    // still check for accessor marker
                    if (arr->extra != 0) {
                        Map* pm = (Map*)(uintptr_t)arr->extra;
                        // Phase 5D: IS_ACCESSOR shape-flag dispatch under digit-string name.
                        Item pm_item = (Item){.map = pm};
                        ShapeEntry* _se_idx = js_find_shape_entry(pm_item, ks->chars, (int)ks->len);
                        if (_se_idx && jspd_is_accessor(_se_idx)) {
                            bool slot_found = false;
                            Item slot_val = js_map_get_fast_ext(pm, ks->chars, (int)ks->len, &slot_found);
                            if (slot_found && slot_val.item != JS_DELETED_SENTINEL_VAL) {
                                return (Item){.item = b2it(true)};
                            }
                        }
                        // AT-3: legacy __get_X/__set_X marker fallback retired
                        // (post-AT-1 IS_ACCESSOR shape probe above always succeeds).
                    }
                    return (Item){.item = b2it(false)};
                }
                return (Item){.item = b2it(true)};
            }
            // index out of bounds — check for accessor markers in companion map
            if (arr->extra != 0) {
                Map* pm = (Map*)(uintptr_t)arr->extra;
                // Phase 5D: IS_ACCESSOR shape-flag dispatch under digit-string name.
                Item pm_item = (Item){.map = pm};
                ShapeEntry* _se_idx = js_find_shape_entry(pm_item, ks->chars, (int)ks->len);
                if (_se_idx && jspd_is_accessor(_se_idx)) {
                    bool slot_found = false;
                    Item slot_val = js_map_get_fast_ext(pm, ks->chars, (int)ks->len, &slot_found);
                    if (slot_found && slot_val.item != JS_DELETED_SENTINEL_VAL) {
                        return (Item){.item = b2it(true)};
                    }
                }
                // AT-3: legacy __get_X/__set_X marker fallback retired.
            }
        }
        // check companion map for named (non-index) properties
        {
            Array* arr = obj.array;
            if (arr->extra != 0) {
                Map* pm = (Map*)(uintptr_t)arr->extra;
                bool found = false;
                Item v = js_map_get_fast_ext(pm, ks->chars, (int)ks->len, &found);
                if (found && v.item != JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(true)};
            }
        }
        return (Item){.item = b2it(false)};
    }
    // v18: handle function objects — prototype, name, length, and custom properties
    if (get_type_id(obj) == LMD_TYPE_FUNC) {
        Item k;
        // v41: Symbol keys → __sym_N format
        if (get_type_id(key) == LMD_TYPE_INT && it2i(key) <= -(int64_t)JS_SYMBOL_BASE) {
            int64_t id = -(it2i(key) + (int64_t)JS_SYMBOL_BASE);
            char buf[32];
            snprintf(buf, sizeof(buf), "__sym_%lld", (long long)id);
            k = (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
        } else {
            k = js_to_string(key);
        }
        if (get_type_id(k) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
        String* ks = it2s(k);
        if (!ks) return (Item){.item = b2it(false)};
        // v23: Check properties_map FIRST for deleted/overridden properties
        JsFuncProps* fn = (JsFuncProps*)obj.function;
        if (fn->properties_map.item != 0) {
            bool own = false;
            Item val = js_map_get_fast_ext(fn->properties_map.map, ks->chars, ks->len, &own);
            if (own) {
                // If sentinel, property was deleted — except for "prototype" where the
                // sentinel is used by js_property_set as a "cleared previous non-MAP entry"
                // marker (see v91 in js_property_get). Fall through to the prototype check.
                if (val.item == JS_DELETED_SENTINEL_VAL) {
                    if (!(ks->len == 9 && strncmp(ks->chars, "prototype", 9) == 0))
                        return (Item){.item = b2it(false)};
                } else {
                    return (Item){.item = b2it(true)};
                }
            }
            // Phase-5D: legacy __get_/__set_ accessor-marker probes removed.
            // Phase-4 intercept routes function-property accessors into a single
            // bare-name slot containing a JsAccessorPair, with IS_ACCESSOR shape
            // flag. The bare-name fast probe above returns own=true with a
            // non-sentinel value for IS_ACCESSOR slots.
        }
        // built-in own properties (not overridden/deleted)
        if ((ks->len == 4 && strncmp(ks->chars, "name", 4) == 0) ||
            (ks->len == 6 && strncmp(ks->chars, "length", 6) == 0)) {
            return (Item){.item = b2it(true)};
        }
        // prototype is own only for constructor functions
        if (ks->len == 9 && strncmp(ks->chars, "prototype", 9) == 0) {
            return (Item){.item = b2it(js_func_has_own_prototype(obj))};
        }
        return (Item){.item = b2it(false)};
    }
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    // Convert symbol keys to their internal string representation
    Item k;
    if (get_type_id(key) == LMD_TYPE_INT && it2i(key) <= -(int64_t)JS_SYMBOL_BASE) {
        int64_t id = -(it2i(key) + (int64_t)JS_SYMBOL_BASE);
        char buf[32];
        snprintf(buf, sizeof(buf), "__sym_%lld", (long long)id);
        k = (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
    } else {
        k = js_to_string(key);
    }
    if (get_type_id(k) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* ks = it2s(k);
    if (!ks) return (Item){.item = b2it(false)};
    Map* m = obj.map;
    if (!m || !m->type) return (Item){.item = b2it(false)};
    // Stage A1.8b (R4 routing): tri-state kernel chokepoint.
    //   PRESENT — own own slot, non-sentinel → property exists.
    //   DELETED — own slot is tombstoned; per spec the property does NOT
    //             exist and we MUST NOT fall through to the builtin /
    //             String-wrapper probe (would resurrect deleted builtin).
    //   ABSENT  — no own slot at all; fall through to builtin / String-wrapper.
    {
        JsOwnSlotStatus st = js_ordinary_own_status(obj, ks->chars, (int)ks->len);
        if (st == JS_HAS_PRESENT) return (Item){.item = b2it(true)};
        if (st == JS_HAS_DELETED) return (Item){.item = b2it(false)};
        // JS_HAS_ABSENT — fall through.
    }
    // Slot truly absent: fall back to builtin methods / String-wrapper indexed access.
    {
        // v26: check if this is a prototype Map with builtin methods
        if (js_map_has_builtin_method(m, ks->chars, (int)ks->len)) return (Item){.item = b2it(true)};
        // String wrapper indexed access: new String("abc").hasOwnProperty("0") → true
        if (ks->len > 0 && ks->chars[0] >= '0' && ks->chars[0] <= '9') {
            if (js_class_id((Item){.map = m}) == JS_CLASS_STRING) {
                    bool pv_found = false;
                    Item pv = js_map_get_fast_ext(m, "__primitiveValue__", 18, &pv_found);
                    if (pv_found && get_type_id(pv) == LMD_TYPE_STRING) {
                        String* pv_str = it2s(pv);
                        bool is_idx = true;
                        int64_t idx = 0;
                        for (int ni = 0; ni < (int)ks->len; ni++) {
                            if (ks->chars[ni] < '0' || ks->chars[ni] > '9') { is_idx = false; break; }
                            idx = idx * 10 + (ks->chars[ni] - '0');
                        }
                        if (is_idx && idx >= 0 && idx < (int64_t)(pv_str ? pv_str->len : 0)) {
                            return (Item){.item = b2it(true)};
                        }
                    }
            }
        }
        return (Item){.item = b2it(false)};
    }
}

// =============================================================================
// Object.freeze(obj) — set __frozen__ flag, Object.isFrozen(obj)
// =============================================================================

extern "C" Item js_object_freeze(Item obj) {
    // ES6: non-objects return the argument
    TypeId ot = get_type_id(obj);
    if (ot != LMD_TYPE_MAP && ot != LMD_TYPE_ARRAY && ot != LMD_TYPE_FUNC && ot != LMD_TYPE_ELEMENT) return obj;
    // ES §7.3.16 SetIntegrityLevel("frozen"): for each own key, define with
    // {writable:false, configurable:false} (skip writable for accessors).
    // Routed through js_define_own_property_from_descriptor (Stage A2.5).
    Item keys = js_object_get_own_property_names(obj);
    if (get_type_id(keys) == LMD_TYPE_ARRAY) {
        for (int i = 0; i < keys.array->length; i++) {
            Item key = keys.array->items[i];
            if (get_type_id(key) != LMD_TYPE_STRING) continue;
            String* str_key = it2s(key);
            if (!str_key || str_key->len == 0 || str_key->len >= 200) continue;
            // Determine if this property is an accessor (skip writable bit).
            JsPropertyDescriptor existing;
            memset(&existing, 0, sizeof(existing));
            bool has_existing = js_get_own_property_descriptor(obj,
                str_key->chars, (int)str_key->len, &existing);
            JsPropertyDescriptor pd;
            memset(&pd, 0, sizeof(pd));
            pd.flags |= JS_PD_HAS_CONFIGURABLE;  // configurable=false (bit cleared)
            if (!has_existing || !js_pd_is_accessor(&existing)) {
                pd.flags |= JS_PD_HAS_WRITABLE;  // writable=false (bit cleared)
            }
            js_define_own_property_from_descriptor(obj,
                str_key->chars, (int)str_key->len, &pd, /*is_new_property*/false);
        }
    }
    Item key = (Item){.item = s2it(heap_create_name("__frozen__", 10))};
    js_defprop_set_marker(obj, key, (Item){.item = b2it(true)});
    return obj;
}

extern "C" Item js_object_is_frozen(Item obj) {
    // ES6: non-objects are frozen
    TypeId ot = get_type_id(obj);
    if (ot != LMD_TYPE_MAP && ot != LMD_TYPE_ARRAY && ot != LMD_TYPE_FUNC && ot != LMD_TYPE_ELEMENT)
        return (Item){.item = b2it(true)};
    // For arrays and functions, check via marker system
    if (ot == LMD_TYPE_ARRAY || ot == LMD_TYPE_FUNC) {
        bool found = false;
        Item fv = js_defprop_get_marker(obj, "__frozen__", 10, &found);
        if (found && js_is_truthy(fv)) return (Item){.item = b2it(true)};
        return (Item){.item = b2it(false)};
    }
    if (ot != LMD_TYPE_MAP) return (Item){.item = b2it(true)};
    // fast path: explicitly frozen
    bool fk_found = false;
    Item fv = js_map_get_fast_ext(obj.map, "__frozen__", 10, &fk_found);
    if (fk_found && js_is_truthy(fv)) return (Item){.item = b2it(true)};
    // must be non-extensible
    bool ne_found = false;
    Item nev = js_map_get_fast_ext(obj.map, "__non_extensible__", 17, &ne_found);
    if (!ne_found || !js_is_truthy(nev)) return (Item){.item = b2it(false)};
    // check all own properties are non-configurable and non-writable (or accessor)
    Map* m = obj.map;
    if (!m || !m->type) return (Item){.item = b2it(true)}; // no shape = no properties
    TypeMap* tm = (TypeMap*)m->type;
    ShapeEntry* e = tm->shape;
    while (e) {
        if (e->name) {
            const char* n = e->name->str;
            int nlen = (int)e->name->length;
            if (nlen >= 2 && n[0] == '_' && n[1] == '_') { e = e->next; continue; }
            // Stage A3.4: shape-flag-first via helper (falls back to legacy markers).
            // check non-configurable
            if (js_props_query_configurable(m, e, n, nlen)) return (Item){.item = b2it(false)};
            // accessor properties don't need to be non-writable per ES spec
            bool is_accessor = jspd_is_accessor(e);
            // Phase-5D: legacy __get_/__set_ fallback probe removed.
            // Accessors are detected via IS_ACCESSOR shape flag on the bare-name entry.
            if (!is_accessor) {
                if (js_props_query_writable(m, e, n, nlen)) return (Item){.item = b2it(false)};
            }
        }
        e = e->next;
    }
    return (Item){.item = b2it(true)};
}

// =============================================================================
// Object.seal — mark all properties non-configurable, mark object non-extensible
// =============================================================================

extern "C" Item js_object_seal(Item obj) {
    // ES6: non-objects return the argument
    TypeId ot = get_type_id(obj);
    if (ot != LMD_TYPE_MAP && ot != LMD_TYPE_ARRAY && ot != LMD_TYPE_FUNC && ot != LMD_TYPE_ELEMENT) return obj;
    // ES §7.3.16 SetIntegrityLevel("sealed"): for each own key, define with
    // {configurable:false}. Routed through the kernel (Stage A2.5).
    Item keys = js_object_get_own_property_names(obj);
    if (get_type_id(keys) == LMD_TYPE_ARRAY) {
        for (int i = 0; i < keys.array->length; i++) {
            Item key = keys.array->items[i];
            if (get_type_id(key) != LMD_TYPE_STRING) continue;
            String* str_key = it2s(key);
            if (!str_key || str_key->len == 0 || str_key->len >= 200) continue;
            JsPropertyDescriptor pd;
            memset(&pd, 0, sizeof(pd));
            pd.flags |= JS_PD_HAS_CONFIGURABLE;  // configurable=false (bit cleared)
            js_define_own_property_from_descriptor(obj,
                str_key->chars, (int)str_key->len, &pd, /*is_new_property*/false);
        }
    }
    Item sealed_k = (Item){.item = s2it(heap_create_name("__sealed__", 10))};
    js_defprop_set_marker(obj, sealed_k, (Item){.item = b2it(true)});
    return obj;
}

extern "C" Item js_object_is_sealed(Item obj) {
    // ES6: non-objects are sealed
    TypeId ot = get_type_id(obj);
    if (ot != LMD_TYPE_MAP && ot != LMD_TYPE_ARRAY && ot != LMD_TYPE_FUNC && ot != LMD_TYPE_ELEMENT)
        return (Item){.item = b2it(true)};
    // For arrays and functions, check via marker system
    if (ot == LMD_TYPE_ARRAY || ot == LMD_TYPE_FUNC) {
        bool found = false;
        Item sv = js_defprop_get_marker(obj, "__sealed__", 10, &found);
        if (found && js_is_truthy(sv)) return (Item){.item = b2it(true)};
        Item fv = js_defprop_get_marker(obj, "__frozen__", 10, &found);
        if (found && js_is_truthy(fv)) return (Item){.item = b2it(true)};
        return (Item){.item = b2it(false)};
    }
    if (ot != LMD_TYPE_MAP) return (Item){.item = b2it(true)};
    // fast path: explicitly sealed or frozen
    bool sk_found = false;
    Item sv = js_map_get_fast_ext(obj.map, "__sealed__", 10, &sk_found);
    if (sk_found && js_is_truthy(sv)) return (Item){.item = b2it(true)};
    bool fk_found = false;
    Item fv = js_map_get_fast_ext(obj.map, "__frozen__", 10, &fk_found);
    if (fk_found && js_is_truthy(fv)) return (Item){.item = b2it(true)};
    // must be non-extensible
    bool ne_found = false;
    Item nev = js_map_get_fast_ext(obj.map, "__non_extensible__", 17, &ne_found);
    if (!ne_found || !js_is_truthy(nev)) return (Item){.item = b2it(false)};
    // check all own properties are non-configurable
    Map* m = obj.map;
    if (!m || !m->type) return (Item){.item = b2it(true)}; // no shape = no properties
    TypeMap* tm = (TypeMap*)m->type;
    ShapeEntry* e = tm->shape;
    while (e) {
        if (e->name) {
            const char* n = e->name->str;
            int nlen = (int)e->name->length;
            if (nlen >= 2 && n[0] == '_' && n[1] == '_') { e = e->next; continue; }
            // Stage A3.4: shape-flag-first via helper (falls back to legacy markers).
            if (js_props_query_configurable(m, e, n, nlen)) return (Item){.item = b2it(false)};
        }
        e = e->next;
    }
    return (Item){.item = b2it(true)};
}

// =============================================================================
// Object.preventExtensions / Object.isExtensible
// =============================================================================

extern "C" Item js_object_prevent_extensions(Item obj) {
    // Proxy [[PreventExtensions]] trap
    if (js_is_proxy(obj)) {
        return js_proxy_trap_prevent_extensions(obj);
    }
    // ES6: non-objects return the argument
    TypeId ot = get_type_id(obj);
    if (ot != LMD_TYPE_MAP && ot != LMD_TYPE_ARRAY && ot != LMD_TYPE_FUNC && ot != LMD_TYPE_ELEMENT) return obj;
    Item key = (Item){.item = s2it(heap_create_name("__non_extensible__", 17))};
    js_defprop_set_marker(obj, key, (Item){.item = b2it(true)});
    return obj;
}

extern "C" Item js_object_is_extensible(Item obj) {
    // Proxy [[IsExtensible]] trap
    if (js_is_proxy(obj)) {
        return js_proxy_trap_is_extensible(obj);
    }
    // ES6: non-objects are not extensible
    TypeId ot = get_type_id(obj);
    if (ot != LMD_TYPE_MAP && ot != LMD_TYPE_ARRAY && ot != LMD_TYPE_FUNC && ot != LMD_TYPE_ELEMENT)
        return (Item){.item = b2it(false)};
    if (ot == LMD_TYPE_ARRAY) {
        // Arrays: check companion map for __non_extensible__ marker
        Array* arr = obj.array;
        if (arr->extra != 0) {
            Map* props = (Map*)(uintptr_t)arr->extra;
            bool found = false;
            Item ne_v = js_map_get_fast_ext(props, "__non_extensible__", 17, &found);
            if (found && js_is_truthy(ne_v)) return (Item){.item = b2it(false)};
            Item sl_v = js_map_get_fast_ext(props, "__sealed__", 10, &found);
            if (found && js_is_truthy(sl_v)) return (Item){.item = b2it(false)};
            Item fr_v = js_map_get_fast_ext(props, "__frozen__", 10, &found);
            if (found && js_is_truthy(fr_v)) return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(true)};
    }
    if (ot == LMD_TYPE_FUNC) {
        // Functions: check properties_map for __non_extensible__ marker
        JsFuncProps* fn = (JsFuncProps*)obj.function;
        if (get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
            Map* pm = fn->properties_map.map;
            bool found = false;
            Item ne_v = js_map_get_fast_ext(pm, "__non_extensible__", 17, &found);
            if (found && js_is_truthy(ne_v)) return (Item){.item = b2it(false)};
            Item sl_v = js_map_get_fast_ext(pm, "__sealed__", 10, &found);
            if (found && js_is_truthy(sl_v)) return (Item){.item = b2it(false)};
            Item fr_v = js_map_get_fast_ext(pm, "__frozen__", 10, &found);
            if (found && js_is_truthy(fr_v)) return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(true)};
    }
    if (ot != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    // non-extensible if explicitly marked, or sealed, or frozen
    Item ne_k = (Item){.item = s2it(heap_create_name("__non_extensible__", 17))};
    Item ne_v = map_get(obj.map, ne_k);
    if (js_is_truthy(ne_v)) return (Item){.item = b2it(false)};
    Item sl_k = (Item){.item = s2it(heap_create_name("__sealed__", 10))};
    Item sl_v = map_get(obj.map, sl_k);
    if (js_is_truthy(sl_v)) return (Item){.item = b2it(false)};
    Item fr_k = (Item){.item = s2it(heap_create_name("__frozen__", 10))};
    Item fr_v = map_get(obj.map, fr_k);
    if (js_is_truthy(fr_v)) return (Item){.item = b2it(false)};
    return (Item){.item = b2it(true)};
}

// =============================================================================
// Number static methods — Number.isInteger, Number.isFinite, Number.isNaN, Number.isSafeInteger
// =============================================================================

extern "C" Item js_number_is_integer(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) {
        if (type == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(true)};
    }
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        return (Item){.item = b2it(isfinite(d) && d == floor(d))};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_number_is_finite(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) {
        if (type == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(true)};
    }
    if (type == LMD_TYPE_FLOAT) {
        return (Item){.item = b2it(isfinite(it2d(value)))};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_number_is_nan(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_FLOAT) {
        return (Item){.item = b2it(isnan(it2d(value)))};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_number_is_safe_integer(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) {
        if (type == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE) return (Item){.item = b2it(false)};
        int64_t v = it2i(value);
        return (Item){.item = b2it(v >= -9007199254740991LL && v <= 9007199254740991LL)};
    }
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        return (Item){.item = b2it(isfinite(d) && d == floor(d) && fabs(d) <= 9007199254740991.0)};
    }
    return (Item){.item = b2it(false)};
}

// =============================================================================
// Array.from(iterable) — convert array-like to array
// =============================================================================

extern "C" Item js_array_from(Item iterable) {
    extern Item js_throw_type_error(const char* msg);
    TypeId tid = get_type_id(iterable);
    // spec: TypeError if items is null or undefined
    if (tid == LMD_TYPE_NULL || iterable.item == ITEM_JS_UNDEFINED) {
        js_throw_type_error("Cannot convert undefined or null to object");
        return js_array_new(0);
    }
    if (tid == LMD_TYPE_ARRAY) {
        // shallow copy
        Array* src = iterable.array;
        Item result = js_array_new(src->length);
        Array* dst = result.array;
        memcpy(dst->items, src->items, src->length * sizeof(Item));
        dst->length = src->length;
        return result;
    }
    // TypedArray: convert each element to a JS number in a regular array
    if (js_is_typed_array(iterable)) {
        JsTypedArray* ta = js_get_typed_array_ptr(iterable.map);
        int len = ta->length;
        Item result = js_array_new(0);
        for (int i = 0; i < len; i++) {
            Item idx = (Item){.item = i2it(i)};
            Item val = js_typed_array_get(iterable, idx);
            array_push(result.array, val);
        }
        return result;
    }
    if (tid == LMD_TYPE_STRING) {
        // split string into individual code points (not bytes)
        String* s = it2s(iterable);
        if (!s) return js_array_new(0);
        Item result = js_array_new(0);
        int i = 0;
        while (i < (int)s->len) {
            unsigned char lead = (unsigned char)s->chars[i];
            int cp_len = 1;
            if (lead >= 0xF0 && i + 4 <= (int)s->len)      cp_len = 4;
            else if (lead >= 0xE0 && i + 3 <= (int)s->len)  cp_len = 3;
            else if (lead >= 0xC0 && i + 2 <= (int)s->len)  cp_len = 2;
            int total_len = cp_len;
            // combine WTF-8/CESU-8 surrogate pairs
            if (cp_len == 3 && lead == 0xED && i + 1 < (int)s->len) {
                unsigned char second = (unsigned char)s->chars[i + 1];
                if (second >= 0xA0 && second <= 0xAF) {
                    int next = i + 3;
                    if (next + 2 < (int)s->len &&
                        (unsigned char)s->chars[next] == 0xED) {
                        unsigned char ns = (unsigned char)s->chars[next + 1];
                        if (ns >= 0xB0 && ns <= 0xBF) {
                            total_len = 6;
                        }
                    }
                }
            }
            String* ch = heap_strcpy(&s->chars[i], total_len);
            js_array_push(result, (Item){.item = s2it(ch)});
            i += total_len;
        }
        return result;
    }
    // v20: Array-like objects with .length property (e.g. {0: 'a', 1: 'b', length: 2})
    if (tid == LMD_TYPE_MAP) {
        Item len_key = (Item){.item = s2it(heap_create_name("length", 6))};
        Item len_val = js_property_get(iterable, len_key);
        TypeId lt = get_type_id(len_val);
        if (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) {
            int len = (lt == LMD_TYPE_INT) ? (int)it2i(len_val) : (int)it2d(len_val);
            if (len >= 0) {
                Item result = js_array_new(0);
                for (int i = 0; i < len; i++) {
                    char idx_buf[16];
                    snprintf(idx_buf, sizeof(idx_buf), "%d", i);
                    Item idx_key = (Item){.item = s2it(heap_create_name(idx_buf, strlen(idx_buf)))};
                    Item val = js_property_get(iterable, idx_key);
                    js_array_push(result, val);
                }
                return result;
            }
        }
    }
    // Use js_iterable_to_array for Map, Set, generators, and other iterables
    Item converted = js_iterable_to_array(iterable);
    if (get_type_id(converted) == LMD_TYPE_ARRAY) {
        // shallow copy to return a new array
        Array* src = converted.array;
        Item result = js_array_new(src->length);
        Array* dst = result.array;
        memcpy(dst->items, src->items, src->length * sizeof(Item));
        dst->length = src->length;
        return result;
    }
    return js_array_new(0);
}

// Array.from(iterable, mapFn) — with optional mapper function
extern "C" Item js_array_from_with_mapper(Item iterable, Item mapFn) {
    extern Item js_throw_type_error(const char* msg);
    extern int js_check_exception(void);
    // J39-7 / ES §22.1.2.1 step 3.a: if mapFn is undefined → mapping=false;
    // otherwise, if IsCallable(mapFn) is false → throw TypeError. null,
    // objects, strings, etc. all throw.
    TypeId mft = get_type_id(mapFn);
    bool is_undef = (mapFn.item == ITEM_JS_UNDEFINED) || mft == LMD_TYPE_UNDEFINED;
    if (!is_undef && mft != LMD_TYPE_FUNC) {
        js_throw_type_error("Array.from: mapFn is not a function");
        return js_array_new(0);
    }
    // No mapper: just delegate.
    if (mft != LMD_TYPE_FUNC) return js_array_from(iterable);
    // J39-7 spec §22.1.2.1: source[k] must be read each iteration so that callbacks
    // mutating the source array are observed. For LMD_TYPE_ARRAY and array-like maps,
    // iterate the source live.
    TypeId stid = get_type_id(iterable);
    if (stid == LMD_TYPE_NULL || iterable.item == ITEM_JS_UNDEFINED) {
        js_throw_type_error("Cannot convert undefined or null to object");
        return js_array_new(0);
    }
    if (stid == LMD_TYPE_ARRAY) {
        Array* src = iterable.array;
        Item result = js_array_new(0);
        // Per Array iterator protocol: re-check length each iteration so that
        // iteration stops if source is shrunk by callback.
        for (int64_t i = 0; i < (int64_t)src->length; i++) {
            // re-fetch each iteration — observes mutations
            Item elem = js_array_get(iterable, (Item){.item = i2it((int)i)});
            Item idx_item = (Item){.item = i2it((int)i)};
            Item args[2] = {elem, idx_item};
            Item mapped = js_call_function(mapFn, make_js_undefined(), args, 2);
            if (js_check_exception()) return js_array_new(0);
            js_array_push(result, mapped);
        }
        return result;
    }
    // Fallback: pre-materialize and map.
    Item arr = js_array_from(iterable);
    if (js_check_exception()) return js_array_new(0);
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        Item elem = js_array_get(arr, (Item){.item = i2it(i)});
        Item idx_item = (Item){.item = i2it(i)};
        Item args[2] = {elem, idx_item};
        Item mapped = js_call_function(mapFn, make_js_undefined(), args, 2);
        if (js_check_exception()) return js_array_new(0);
        js_array_set(arr, (Item){.item = i2it(i)}, mapped);
    }
    return arr;
}

// Array.from(iterable, mapFn, thisArg) — with mapper and explicit this value
extern "C" Item js_array_from_with_mapper_this(Item iterable, Item mapFn, Item this_arg) {
    extern Item js_throw_type_error(const char* msg);
    extern int js_check_exception(void);
    // J39-7 / ES §22.1.2.1 step 3.a: validate mapFn before consuming iterable.
    TypeId mft = get_type_id(mapFn);
    bool is_undef = (mapFn.item == ITEM_JS_UNDEFINED) || mft == LMD_TYPE_UNDEFINED;
    if (!is_undef && mft != LMD_TYPE_FUNC) {
        js_throw_type_error("Array.from: mapFn is not a function");
        return js_array_new(0);
    }
    if (mft != LMD_TYPE_FUNC) return js_array_from(iterable);
    TypeId stid = get_type_id(iterable);
    if (stid == LMD_TYPE_NULL || iterable.item == ITEM_JS_UNDEFINED) {
        js_throw_type_error("Cannot convert undefined or null to object");
        return js_array_new(0);
    }
    if (stid == LMD_TYPE_ARRAY) {
        Array* src = iterable.array;
        Item result = js_array_new(0);
        for (int64_t i = 0; i < (int64_t)src->length; i++) {
            Item elem = js_array_get(iterable, (Item){.item = i2it((int)i)});
            Item idx_item = (Item){.item = i2it((int)i)};
            Item args[2] = {elem, idx_item};
            Item mapped = js_call_function(mapFn, this_arg, args, 2);
            if (js_check_exception()) return js_array_new(0);
            js_array_push(result, mapped);
        }
        return result;
    }
    Item arr = js_array_from(iterable);
    if (js_check_exception()) return js_array_new(0);
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        Item elem = js_array_get(arr, (Item){.item = i2it(i)});
        Item idx_item = (Item){.item = i2it(i)};
        Item args[2] = {elem, idx_item};
        Item mapped = js_call_function(mapFn, this_arg, args, 2);
        if (js_check_exception()) return js_array_new(0);
        js_array_set(arr, (Item){.item = i2it(i)}, mapped);
    }
    return arr;
}

// =============================================================================
// JSON.parse(str) — parse JSON string to Lambda object
// =============================================================================

extern Input* js_input;

extern "C" Item js_json_parse(Item str_item) {
    Item str_val = js_to_string(str_item);
    // v90: Check for exception from ToPrimitive (e.g., getter-defined valueOf/toString that throws)
    if (js_check_exception()) return ItemNull;
    String* s = it2s(str_val);
    if (!s || s->len == 0) {
        // empty string is not valid JSON
        js_throw_syntax_error((Item){.item = s2it(heap_create_name("Unexpected end of JSON input"))});
        return ItemNull;
    }

    // null-terminate for the parser
    char* buf = (char*)alloca(s->len + 1);
    memcpy(buf, s->chars, s->len);
    buf[s->len] = '\0';

    if (!js_input) {
        log_error("js_json_parse: no input context");
        return ItemNull;
    }

    bool ok = false;
    Item result = parse_json_to_item_strict(js_input, buf, &ok);
    if (!ok) {
        js_throw_syntax_error((Item){.item = s2it(heap_create_name("Unexpected token in JSON"))});
        return ItemNull;
    }
    return result;
}

// v20: walk parsed JSON tree bottom-up, applying reviver function
static Item js_json_revive(Item holder, Item key, Item reviver) {
    Item val = js_property_access(holder, key);
    TypeId vtype = get_type_id(val);

    if (vtype == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(val);
        for (int64_t i = 0; i < len; i++) {
            Item idx_str = js_to_string((Item){.item = i2it((int)i)});
            Item new_elem = js_json_revive(val, idx_str, reviver);
            if (get_type_id(new_elem) == LMD_TYPE_UNDEFINED) {
                js_delete_property(val, idx_str);
            } else {
                js_array_set(val, (Item){.item = i2it((int)i)}, new_elem);
            }
        }
    } else if (vtype == LMD_TYPE_MAP) {
        Item keys = js_object_keys(val);
        int64_t klen = js_array_length(keys);
        for (int64_t i = 0; i < klen; i++) {
            Item k = js_array_get(keys, (Item){.item = i2it((int)i)});
            Item new_val = js_json_revive(val, k, reviver);
            if (get_type_id(new_val) == LMD_TYPE_UNDEFINED) {
                js_delete_property(val, k);
            } else {
                js_property_set(val, k, new_val);
            }
        }
    }

    // Call reviver with (key, val)
    Item args[2] = {key, val};
    return js_call_function(reviver, holder, args, 2);
}

extern "C" Item js_json_parse_full(Item str_item, Item reviver) {
    Item result = js_json_parse(str_item);
    if (result.item == ItemNull.item) return result;

    if (get_type_id(reviver) == LMD_TYPE_FUNC) {
        // Create a wrapper object {"": result} as the root holder
        Item wrapper = js_new_object();
        Item empty_key = (Item){.item = s2it(heap_create_name("", 0))};
        js_property_set(wrapper, empty_key, result);
        result = js_json_revive(wrapper, empty_key, reviver);
    }
    return result;
}

// =============================================================================
// JSON.stringify(value, replacer?, space?) — convert Lambda object to JSON string
// =============================================================================

// v20: forward declarations for recursive JSON serialization
// Circular reference detection for JSON.stringify
#define JSON_STRINGIFY_MAX_DEPTH 1024

// Forward declaration: check if an Item is a JS Symbol (encoded as negative int)
static bool js_is_symbol_item(Item item);

static bool js_stringify_value(StrBuf* sb, Item value, Item replacer, Item replacer_array,
                               const char* gap, int depth, Item holder, Item key,
                               void** visited, int visited_count);
static void js_stringify_escape_string(StrBuf* sb, const char* s, int len);

static void js_stringify_escape_string(StrBuf* sb, const char* s, int len) {
    strbuf_append_char(sb, '"');
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"': strbuf_append_str_n(sb, "\\\"", 2); break;
            case '\\': strbuf_append_str_n(sb, "\\\\", 2); break;
            case '\b': strbuf_append_str_n(sb, "\\b", 2); break;
            case '\f': strbuf_append_str_n(sb, "\\f", 2); break;
            case '\n': strbuf_append_str_n(sb, "\\n", 2); break;
            case '\r': strbuf_append_str_n(sb, "\\r", 2); break;
            case '\t': strbuf_append_str_n(sb, "\\t", 2); break;
            default:
                if (c < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", c);
                    strbuf_append_str_n(sb, esc, 6);
                } else if (c >= 0xED && c <= 0xEF && i + 2 < len) {
                    // Possible UTF-8 surrogate sequence (U+D800-U+DFFF) or BMP chars
                    unsigned char c2 = (unsigned char)s[i + 1];
                    unsigned char c3 = (unsigned char)s[i + 2];
                    if ((c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
                        unsigned int cp = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                        if (cp >= 0xD800 && cp <= 0xDFFF) {
                            // ES2019: lone surrogate → \uXXXX escape
                            char esc[8];
                            snprintf(esc, sizeof(esc), "\\u%04x", cp);
                            strbuf_append_str_n(sb, esc, 6);
                            i += 2;
                        } else {
                            strbuf_append_char(sb, (char)c);
                        }
                    } else {
                        strbuf_append_char(sb, (char)c);
                    }
                } else {
                    strbuf_append_char(sb, (char)c);
                }
                break;
        }
    }
    strbuf_append_char(sb, '"');
}

static void js_stringify_indent(StrBuf* sb, const char* gap, int depth) {
    if (!gap || !gap[0]) return;
    strbuf_append_char(sb, '\n');
    for (int i = 0; i < depth; i++) {
        strbuf_append_str_n(sb, gap, (int)strlen(gap));
    }
}

// returns true if the value was serialized, false if it's undefined/function/symbol
// (the caller handles the "skip" vs "null" difference for arrays vs objects)
static bool js_stringify_value(StrBuf* sb, Item value, Item replacer, Item replacer_array,
                               const char* gap, int depth, Item holder, Item key,
                               void** visited, int visited_count) {
    // ES spec SerializeJSONProperty steps:
    // Step 2: toJSON first
    TypeId vtype = get_type_id(value);
    if (vtype == LMD_TYPE_MAP || vtype == LMD_TYPE_ARRAY) {
        Item toJSON_name = (Item){.item = s2it(heap_create_name("toJSON", 6))};
        Item toJSON_fn = js_property_access(value, toJSON_name);
        if (get_type_id(toJSON_fn) == LMD_TYPE_FUNC) {
            Item args[1] = {key};
            value = js_call_function(toJSON_fn, value, args, 1);
            vtype = get_type_id(value);
        }
    }

    // Step 3: Apply replacer function
    if (get_type_id(replacer) == LMD_TYPE_FUNC) {
        Item args[2] = {key, value};
        value = js_call_function(replacer, holder, args, 2);
        vtype = get_type_id(value);
    }

    // Step 4: Unwrap Boolean/Number/String/BigInt wrapper objects
    if (vtype == LMD_TYPE_MAP) {
        JsClass cls = js_class_id(value);
        if (cls == JS_CLASS_BOOLEAN || cls == JS_CLASS_NUMBER || cls == JS_CLASS_STRING || cls == JS_CLASS_BIGINT) {
            bool pv_own = false;
            Item pv = js_map_get_fast_ext(value.map, "__primitiveValue__", 18, &pv_own);
            if (pv_own) {
                if (cls == JS_CLASS_BOOLEAN) {
                    value = pv;
                    vtype = get_type_id(value);
                } else if (cls == JS_CLASS_NUMBER) {
                    value = js_to_number(pv);
                    vtype = get_type_id(value);
                } else if (cls == JS_CLASS_STRING) {
                    value = js_to_string(pv);
                    vtype = get_type_id(value);
                } else if (cls == JS_CLASS_BIGINT) {
                    // ES spec step 10: BigInt → TypeError
                    js_throw_type_error("Do not know how to serialize a BigInt");
                    return false;
                }
            }
        }
    }

    // Step 5-8: undefined, function, symbol → return false (not serialized)
    if (vtype == LMD_TYPE_UNDEFINED || vtype == LMD_TYPE_FUNC
        || js_is_symbol_item(value) || value.item == ITEM_JS_UNDEFINED) {
        return false;
    }

    // BigInt → TypeError (ES spec §24.5.2.9 step 10)
    if (vtype == LMD_TYPE_DECIMAL) {
        Decimal* dec = (Decimal*)(value.item & 0x00FFFFFFFFFFFFFF);
        if (dec && dec->unlimited == DECIMAL_BIGINT) {
            js_throw_type_error("Do not know how to serialize a BigInt");
            return false;
        }
    }
    if (value.item == ItemNull.item) {
        strbuf_append_str_n(sb, "null", 4);
        return true;
    }

    // Boolean
    if (vtype == LMD_TYPE_BOOL) {
        if (it2b(value)) strbuf_append_str_n(sb, "true", 4);
        else strbuf_append_str_n(sb, "false", 5);
        return true;
    }

    // Number
    if (vtype == LMD_TYPE_INT || vtype == LMD_TYPE_INT64) {
        char buf[32];
        int64_t n = it2i(value);
        int len = snprintf(buf, sizeof(buf), "%lld", (long long)n);
        strbuf_append_str_n(sb, buf, len);
        return true;
    }
    if (vtype == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        if (d != d || d == (1.0/0.0) || d == (-1.0/0.0)) {
            strbuf_append_str_n(sb, "null", 4); // NaN, Infinity → null
            return true;
        }
        // Negative zero → "0"
        if (d == 0.0) {
            strbuf_append_str_n(sb, "0", 1);
            return true;
        }
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "%.17g", d);
        strbuf_append_str_n(sb, buf, len);
        return true;
    }

    // String
    if (vtype == LMD_TYPE_STRING) {
        String* s = it2s(value);
        if (!s) { strbuf_append_str_n(sb, "null", 4); return true; }
        js_stringify_escape_string(sb, s->chars, (int)s->len);
        return true;
    }

    // Circular reference detection for arrays and objects
    if (vtype == LMD_TYPE_ARRAY || vtype == LMD_TYPE_MAP) {
        void* ptr = (vtype == LMD_TYPE_ARRAY) ? (void*)value.array : (void*)value.map;
        for (int vi = 0; vi < visited_count; vi++) {
            if (visited[vi] == ptr) {
                Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
                Item msg = (Item){.item = s2it(heap_create_name("Converting circular structure to JSON"))};
                js_throw_value(js_new_error_with_name(tn, msg));
                return true;
            }
        }
        if (depth >= JSON_STRINGIFY_MAX_DEPTH) {
            strbuf_append_str_n(sb, "null", 4);
            return true;
        }
        // Push onto visited stack
        if (visited_count < JSON_STRINGIFY_MAX_DEPTH) {
            visited[visited_count] = ptr;
            visited_count++;
        }
    }

    // Array
    if (vtype == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(value);
        if (len == 0) {
            strbuf_append_str_n(sb, "[]", 2);
            return true;
        }
        strbuf_append_char(sb, '[');
        for (int64_t i = 0; i < len; i++) {
            if (i > 0) strbuf_append_char(sb, ',');
            js_stringify_indent(sb, gap, depth + 1);
            Item idx_key = js_to_string((Item){.item = i2it((int)i)});
            Item elem = js_array_get(value, (Item){.item = i2it((int)i)});
            // serialize element; if undefined/function/symbol, write "null" in array context
            bool wrote = js_stringify_value(sb, elem, replacer, replacer_array, gap, depth + 1,
                                            value, idx_key, visited, visited_count);
            if (!wrote) {
                strbuf_append_str_n(sb, "null", 4);
            }
        }
        js_stringify_indent(sb, gap, depth);
        strbuf_append_char(sb, ']');
        return true;
    }

    // Map (object)
    if (vtype == LMD_TYPE_MAP) {
        Item keys;
        // Use replacer_array (PropertyList) if provided, otherwise own keys
        if (get_type_id(replacer_array) == LMD_TYPE_ARRAY) {
            keys = replacer_array;
        } else {
            keys = js_object_keys(value);
        }

        int64_t klen = js_array_length(keys);
        strbuf_append_char(sb, '{');
        bool first = true;
        for (int64_t i = 0; i < klen; i++) {
            Item k = js_array_get(keys, (Item){.item = i2it((int)i)});
            Item k_str = js_to_string(k);
            Item v = js_property_access(value, k_str);

            // Use a temporary buffer to serialize the value
            StrBuf* tmp = strbuf_new();
            bool wrote = js_stringify_value(tmp, v, replacer, replacer_array, gap, depth + 1,
                                            value, k_str, visited, visited_count);
            if (!wrote) {
                // undefined/function/symbol → skip this key in objects
                strbuf_free(tmp);
                continue;
            }

            if (!first) strbuf_append_char(sb, ',');
            first = false;
            js_stringify_indent(sb, gap, depth + 1);
            String* ks = it2s(k_str);
            if (ks) js_stringify_escape_string(sb, ks->chars, (int)ks->len);
            else strbuf_append_str_n(sb, "\"\"", 2);
            strbuf_append_char(sb, ':');
            if (gap && gap[0]) strbuf_append_char(sb, ' ');
            strbuf_append_str_n(sb, tmp->str, (int)tmp->length);
            strbuf_free(tmp);
        }
        if (!first) js_stringify_indent(sb, gap, depth);
        strbuf_append_char(sb, '}');
        return true;
    }

    // Fallback: try toString
    Item sval = js_to_string(value);
    String* ss = it2s(sval);
    if (ss) js_stringify_escape_string(sb, ss->chars, (int)ss->len);
    else strbuf_append_str_n(sb, "null", 4);
    return true;
}

extern "C" Item js_json_stringify_full(Item value, Item replacer, Item space) {
    // Process space parameter
    // ES spec §24.5.3 step 5: unwrap Number/String wrapper objects
    if (get_type_id(space) == LMD_TYPE_MAP) {
        JsClass cls = js_class_id(space);
        if (cls == JS_CLASS_NUMBER) {
            space = js_to_number(space);
        } else if (cls == JS_CLASS_STRING) {
            space = js_to_string(space);
        }
    }
    char gap_buf[11] = {0};
    const char* gap = "";

    TypeId space_type = get_type_id(space);
    if (space_type == LMD_TYPE_INT || space_type == LMD_TYPE_INT64 || space_type == LMD_TYPE_FLOAT) {
        double d = (space_type == LMD_TYPE_FLOAT) ? it2d(space) : (double)it2i(space);
        int n = (int)d;  // ToInteger: truncate toward zero
        if (n < 0) n = 0;
        if (n > 10) n = 10;
        if (n > 0) {
            memset(gap_buf, ' ', n);
            gap_buf[n] = '\0';
            gap = gap_buf;
        }
    } else if (space_type == LMD_TYPE_STRING) {
        String* space_str = it2s(space);
        if (space_str && space_str->len > 0) {
            int n = (int)space_str->len;
            if (n > 10) n = 10;
            memcpy(gap_buf, space_str->chars, n);
            gap_buf[n] = '\0';
            gap = gap_buf;
        }
    }

    // Build PropertyList from replacer array (with deduplication)
    Item replacer_func = ItemNull;
    Item replacer_array = ItemNull;

    if (get_type_id(replacer) == LMD_TYPE_FUNC) {
        replacer_func = replacer;
    } else if (get_type_id(replacer) == LMD_TYPE_ARRAY) {
        // ES spec step 4.b-f: Build PropertyList with deduplication
        int64_t rlen = js_array_length(replacer);
        Item prop_list = js_array_new(0);
        for (int64_t i = 0; i < rlen; i++) {
            Item v = js_array_get(replacer, (Item){.item = i2it((int)i)});
            TypeId vt = get_type_id(v);
            Item item = ItemNull;
            if (vt == LMD_TYPE_STRING) {
                item = v;
            } else if (vt == LMD_TYPE_INT || vt == LMD_TYPE_INT64 || vt == LMD_TYPE_FLOAT) {
                item = js_to_string(v);
            } else if (vt == LMD_TYPE_MAP) {
                // Check for String or Number wrapper objects
                JsClass cls = js_class_id(v);
                if (cls == JS_CLASS_STRING || cls == JS_CLASS_NUMBER) {
                    item = js_to_string(v);
                }
            }
            // Skip undefined/null entries and duplicates
            if (item.item == ItemNull.item) continue;
            // Check for duplicate
            bool dup = false;
            int64_t plen = js_array_length(prop_list);
            String* item_str = it2s(item);
            for (int64_t j = 0; j < plen; j++) {
                Item existing = js_array_get(prop_list, (Item){.item = i2it((int)j)});
                String* ex_str = it2s(existing);
                if (item_str && ex_str && item_str->len == ex_str->len &&
                    strncmp(item_str->chars, ex_str->chars, item_str->len) == 0) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                js_array_push(prop_list, item);
            }
        }
        replacer_array = prop_list;
    }

    // Create wrapper object per spec step 9-10
    StrBuf* sb = strbuf_new();
    Item empty_key = (Item){.item = s2it(heap_create_name("", 0))};
    Item holder = js_new_object();
    js_property_set(holder, empty_key, value);
    void* visited_stack[JSON_STRINGIFY_MAX_DEPTH];

    // Call SerializeJSONProperty — it handles toJSON, replacer, unwrap, and undefined check
    bool wrote = js_stringify_value(sb, value, replacer_func, replacer_array, gap, 0,
                                    holder, empty_key, visited_stack, 0);
    if (!wrote) {
        strbuf_free(sb);
        return make_js_undefined();
    }

    String* result = heap_strcpy(sb->str, (int)sb->length);
    strbuf_free(sb);
    return (Item){.item = s2it(result)};
}

extern "C" Item js_json_stringify(Item value) {
    return js_json_stringify_full(value, ItemNull, ItemNull);
}

// =============================================================================
// delete operator — remove property from object
// =============================================================================

extern "C" Item js_delete_property(Item obj, Item key) {
    // TypeError if base is null or undefined (non-object-coercible)
    // But only if there's no pending exception (e.g. from an unresolvable reference)
    if (obj.item == ITEM_NULL || obj.item == ITEM_JS_UNDEFINED) {
        extern int js_check_exception(void);
        if (!js_check_exception()) {
            String* sk = (get_type_id(key) == LMD_TYPE_STRING) ? it2s(key) : NULL;
            const char* base = (obj.item == ITEM_NULL) ? "null" : "undefined";
            char msg[256];
            snprintf(msg, sizeof(msg), "Cannot delete property '%.*s' of %s",
                     sk ? (int)sk->len : 0, sk ? sk->chars : "", base);
            Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
            Item em = (Item){.item = s2it(heap_create_name(msg, strlen(msg)))};
            js_throw_value(js_new_error_with_name(tn, em));
        }
        return (Item){.item = b2it(false)};
    }
    // Proxy [[Delete]] trap
    if (js_is_proxy(obj)) {
        return js_proxy_trap_delete(obj, key);
    }
    // v23: Handle function property deletion (name, length, prototype, custom)
    if (get_type_id(obj) == LMD_TYPE_FUNC) {
        JsFuncProps* fn = (JsFuncProps*)obj.function;
        // Ensure properties_map exists
        if (fn->properties_map.item == 0) {
            fn->properties_map = js_new_object();
            heap_register_gc_root(&fn->properties_map.item);
        }
        // Check non-configurable: prototype is non-configurable by default for constructors
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* sk = it2s(key);
            if (sk && sk->len == 9 && strncmp(sk->chars, "prototype", 9) == 0) {
                if (js_func_has_own_prototype(obj))
                    return (Item){.item = b2it(false)}; // prototype is non-configurable
                return (Item){.item = b2it(true)}; // non-constructors don't have prototype
            }
            // Honor __nc_<key> non-configurable marker on properties_map.
            if (sk && sk->len > 0 && sk->len < 200 &&
                fn->properties_map.item != 0 &&
                get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
                // Stage A3.2: shape-flag-first non-configurable check on fn props map.
                ShapeEntry* _se = js_find_shape_entry(fn->properties_map, sk->chars, (int)sk->len);
                if (!js_props_query_configurable(fn->properties_map.map, _se,
                                                  sk->chars, (int)sk->len)) {
                    return (Item){.item = b2it(false)};
                }
            }
        }
        // Mark as deleted in properties_map. The bare-key sentinel write
        // below is load-bearing for FUNC's virtual properties (length,
        // name, prototype): they're computed from struct fields and never
        // materialized in properties_map's shape, so the FUNC `get` branch
        // intercepts the sentinel slot to shadow the virtual property.
        //
        // A2-T9 (post AT-4 Option B): the legacy `__nw_/__ne_/__nc_<key>`
        // marker-tombstone loop that used to follow has been removed. With
        // AT-4 Option B, named-property reads via `js_props_query_*` no
        // longer consult those markers — the shape flag is authoritative —
        // so tombstoning them was dead work. The shape-aware
        // non-configurable check above (via `js_props_query_configurable`)
        // is the spec-correct gate; if it returns true we proceed with the
        // sentinel write. The original WONTFIX (routing through
        // `js_ordinary_delete` regressed 35 S15_*_A9 tests because the
        // kernel short-circuits when the slot is absent) still applies —
        // FUNC virtual-shadow needs the unconditional sentinel write that
        // the spec-pure kernel won't emit. Keep the inline write.
        js_property_set(fn->properties_map, key, (Item){.item = JS_DELETED_SENTINEL_VAL});
        // A2-T8c dual-write: stamp JSPD_DELETED on the properties_map shape
        // entry so readers can detect tombstones via shape (in addition to
        // the slot-value sentinel that this branch writes for FUNC virtual
        // properties).
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* sk = it2s(key);
            if (sk && sk->len > 0) {
                js_shape_entry_set_deleted(fn->properties_map, sk->chars, (int)sk->len, /*is_deleted=*/true);
            }
        }
        return (Item){.item = b2it(true)};
    }
    // v25: Handle array element deletion — set element to sentinel to create "hole"
    if (get_type_id(obj) == LMD_TYPE_ARRAY) {
        Array* arr = obj.array;
        // Convert key to numeric index
        int64_t idx = -1;
        if (get_type_id(key) == LMD_TYPE_INT) {
            idx = it2i(key);
        } else if (get_type_id(key) == LMD_TYPE_STRING) {
            String* sk = it2s(key);
            if (sk && sk->len > 0 && sk->len <= 10) {
                bool is_num = true;
                int64_t n = 0;
                for (int i = 0; i < (int)sk->len; i++) {
                    char c = sk->chars[i];
                    if (c < '0' || c > '9') { is_num = false; break; }
                    n = n * 10 + (c - '0');
                }
                if (is_num && !(sk->len > 1 && sk->chars[0] == '0')) idx = n;
            }
        }
        if (idx >= 0 && idx < arr->length) {
            // v27: check __nc_ (non-configurable) marker before deleting
            if (arr->extra != 0) {
                // Stage A1: ToPropertyKey — uniform stringification.
                Item k_str = js_to_property_key(key);
                if (get_type_id(k_str) == LMD_TYPE_STRING) {
                    String* ks = it2s(k_str);
                    if (ks) {
                        Map* pm = (Map*)(uintptr_t)arr->extra;
                        // Stage A3.2: shape-flag-first non-configurable check.
                        ShapeEntry* _se = js_find_shape_entry(obj, ks->chars, (int)ks->len);
                        if (!js_props_query_configurable(pm, _se, ks->chars, (int)ks->len)) {
                            return (Item){.item = b2it(false)};
                        }
                    }
                }
            }
            arr->items[idx] = (Item){.item = JS_DELETED_SENTINEL_VAL};
            // also clear any descriptor markers/accessors in companion map so the
            // index is no longer treated as an own property after delete.
            if (arr->extra != 0) {
                // Stage A1: ToPropertyKey — uniform stringification.
                Item k_str = js_to_property_key(key);
                if (get_type_id(k_str) == LMD_TYPE_STRING) {
                    String* ks = it2s(k_str);
                    if (ks && ks->len > 0 && ks->len < 200) {
                        Item pm_item = (Item){.item = (uint64_t)(uintptr_t)arr->extra | ((uint64_t)LMD_TYPE_MAP << 56)};
                        const char* prefixes[] = {"__get_", "__set_", "__nw_", "__ne_", "__nc_"};
                        for (int pi = 0; pi < 5; pi++) {
                            char mk[256];
                            snprintf(mk, sizeof(mk), "%s%.*s", prefixes[pi], (int)ks->len, ks->chars);
                            Item mk_item = (Item){.item = s2it(heap_create_name(mk, strlen(mk)))};
                            js_property_set(pm_item, mk_item, (Item){.item = JS_DELETED_SENTINEL_VAL});
                        }
                        // Phase 5 / A2-T3: clear IS_ACCESSOR shape flag on the
                        // bare-key slot (which holds JsAccessorPair*) before
                        // tombstoning, so reads no longer dispatch to the
                        // deleted accessor. Routed through the per-Map clone
                        // primitive so sibling Maps sharing this TypeMap
                        // (shape cache) keep their IS_ACCESSOR untouched.
                        ShapeEntry* _se = js_find_shape_entry(pm_item, ks->chars, (int)ks->len);
                        if (_se && jspd_is_accessor(_se)) {
                            js_shape_entry_set_accessor(pm_item, ks->chars, (int)ks->len, /*is_accessor=*/false);
                        }
                        Item bare_k = (Item){.item = s2it(heap_create_name(ks->chars, ks->len))};
                        js_property_set(pm_item, bare_k, (Item){.item = JS_DELETED_SENTINEL_VAL});
                        // A2-T8c dual-write: stamp JSPD_DELETED on the
                        // companion-map shape entry under the digit-string
                        // name (post-AT-1 the entry exists for accessors
                        // installed via intercept; no-op otherwise, which
                        // is fine — the slot-value sentinel still applies).
                        js_shape_entry_set_deleted(pm_item, ks->chars, (int)ks->len, /*is_deleted=*/true);
                    }
                }
            }
            return (Item){.item = b2it(true)};
        }
        // Non-numeric or out-of-range key: route through Stage A1.12 kernel
        // on the array's companion map. Kernel performs the same configurable
        // check (via js_props_query_configurable), tombstones the 5 marker
        // prefixes, clears IS_ACCESSOR shape-flag, and writes the bare-key
        // sentinel — superset of what the legacy code did inline.
        if (arr->extra != 0) {
            Map* pm = (Map*)(uintptr_t)arr->extra;
            Item pm_item = (Item){.item = (uint64_t)(uintptr_t)pm | ((uint64_t)LMD_TYPE_MAP << 56)};
            // Stage A1: ToPropertyKey so Symbol keys (__sym_N) and FLOAT keys
            // are canonicalized identically to define-property time.
            Item k = js_to_property_key(key);
            if (get_type_id(k) == LMD_TYPE_STRING) {
                String* ks = it2s(k);
                if (ks && ks->len > 0 && ks->len < 200) {
                    if (!js_ordinary_delete(pm_item, ks->chars, (int)ks->len)) {
                        return (Item){.item = b2it(false)};
                    }
                }
            }
        }
        return (Item){.item = b2it(true)};
    }
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(true)};
    // Stage A1: canonicalize key via js_to_property_key (Symbol→__sym_N,
    // INT/FLOAT→decimal string) so marker tombstones below match the shape
    // entry created at defineProperty time.
    {
        TypeId kt0 = get_type_id(key);
        if (kt0 == LMD_TYPE_INT || kt0 == LMD_TYPE_FLOAT) {
            key = js_to_property_key(key);
        }
    }
    // v95: Track __sym_1 deletion on a map (Array.prototype[Symbol.iterator] may be deleted)
    extern int g_array_sym_iter_ever_set;
    if (!g_array_sym_iter_ever_set && get_type_id(key) == LMD_TYPE_STRING) {
        String* _dk = it2s(key);
        if (_dk && _dk->len == 7 && strncmp(_dk->chars, "__sym_1", 7) == 0)
            g_array_sym_iter_ever_set = 1;
    }
    // v16: Frozen objects reject property deletion
    {
        Map* m = obj.map;
        bool frozen_found = false;
        Item frozen_val = js_map_get_fast_ext(m, "__frozen__", 10, &frozen_found);
        if (frozen_found && js_is_truthy(frozen_val)) {
            if (js_strict_mode) {
                String* sk = (get_type_id(key) == LMD_TYPE_STRING) ? it2s(key) : NULL;
                char msg[256];
                snprintf(msg, sizeof(msg), "Cannot delete property '%.*s' of a frozen object",
                         sk ? (int)sk->len : 0, sk ? sk->chars : "");
                Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                Item em = (Item){.item = s2it(heap_create_name(msg))};
                js_throw_value(js_new_error_with_name(tn, em));
            }
            return (Item){.item = b2it(false)};
        }
    }
    // v16: Non-configurable properties cannot be deleted
    if (get_type_id(key) == LMD_TYPE_STRING) {
        String* str_key = it2s(key);
        if (str_key && str_key->len > 0 && str_key->len < 200) {
            // Phase 2c fast path: consult ShapeEntry::flags first.
            int fp = js_prop_attrs_fast_path(obj, str_key->chars, (int)str_key->len, JSPD_NON_CONFIGURABLE);
            bool is_nc = false;
            if (fp == 0) {
                is_nc = true;
            } else if (fp == -1) {
                char nc_key[256];
                snprintf(nc_key, sizeof(nc_key), "__nc_%.*s", (int)str_key->len, str_key->chars);
                bool nc_found = false;
                Item nc_val = js_map_get_fast_ext(obj.map, nc_key, (int)strlen(nc_key), &nc_found);
                if (nc_found && nc_val.item == JS_DELETED_SENTINEL_VAL) nc_found = false;
                if (nc_found && js_is_truthy(nc_val)) is_nc = true;
            }
            if (is_nc) {
                if (js_strict_mode) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Cannot delete property '%.*s' of #<Object>",
                             (int)str_key->len, str_key->chars);
                    Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                    Item em = (Item){.item = s2it(heap_create_name(msg))};
                    js_throw_value(js_new_error_with_name(tn, em));
                }
                return (Item){.item = b2it(false)};
            }
        }
    }
    // Mark property as deleted using sentinel value.
    // Object.keys, hasOwnProperty, in, and JSON.stringify skip sentinel entries.
    //
    // v24: Clear __nw_/__nc_/__ne_ markers BEFORE setting sentinel.
    // js_property_set enforces non-writable checks, which would silently reject
    // the sentinel write for properties defined via Object.defineProperty with
    // writable:false (the default). We clear markers to false first, then
    // use js_property_set for the sentinel (which may trigger shape rebuild).
    //
    // Phase 5 fix: route the marker clears through js_property_set so that
    // js_dual_write_marker_flags clears the corresponding shape-entry attribute
    // flag (NON_WRITABLE/NON_ENUMERABLE/NON_CONFIGURABLE) on the target property.
    // fn_map_set bypasses dual-write, leaving stale flags that cause the
    // subsequent sentinel write to be rejected by the non-writable fast path.
    if (get_type_id(key) == LMD_TYPE_STRING) {
        String* str_key = it2s(key);
        if (str_key && str_key->len > 0 && str_key->len < 200) {
            // Probe-then-clear pattern. Pre-A2-T5: probed marker map entry only.
            // Post-A2-T5: `js_attr_set_*` may set the shape flag without writing
            // a marker entry, so we must consult the shape flag too. The
            // unified `js_props_query_*` helpers check shape-flag-first then
            // marker-fallback, matching the reader semantics. If the property
            // is currently non-writable / non-configurable / non-enumerable
            // (from either source), clear via the A2.6 helper which now goes
            // through the per-Map shape clone (and falls back to a marker
            // write only if no shape entry exists).
            int kl = (int)str_key->len;
            const char* kc = str_key->chars;
            ShapeEntry* _se = js_find_shape_entry(obj, kc, kl);
            // Clear non-writable (allows sentinel write through js_property_set)
            if (!js_props_query_writable(obj.map, _se, kc, kl))
                js_attr_set_writable(obj, kc, kl, /*writable=*/true);
            // Clear non-configurable
            if (!js_props_query_configurable(obj.map, _se, kc, kl))
                js_attr_set_configurable(obj, kc, kl, /*configurable=*/true);
            // Clear non-enumerable
            if (!js_props_query_enumerable(obj.map, _se, kc, kl))
                js_attr_set_enumerable(obj, kc, kl, /*enumerable=*/true);
            // Clear getter/setter markers via js_property_set with the deleted sentinel.
            // Using js_property_set ensures the slot type can be rebuilt (FUNC→INT) even
            // when the original marker was a function — fn_map_set may reject that
            // narrowing. Writing the sentinel (rather than `false_val`) lets all readers
            // (js_has_own_property, js_in, accessor dispatch) treat the marker as absent
            // via the existing JS_DELETED_SENTINEL_VAL probe. The `__get_/__set_` key
            // prefix already bypasses accessor dispatch in js_property_set.
            // AT-3: legacy __get_<name>/__set_<name> sentinel write retired.
            // Post-AT-1 accessors are stored as IS_ACCESSOR shape entry under
            // the property name; the IS_ACCESSOR flag is cleared and the slot
            // sentinel-written by the block below.
        }
    }
    // For FLOAT-typed fields, fn_map_set's FLOAT→INT widening path converts the
    // INT sentinel to a double, making it unrecognizable. Fix: first set a BOOL
    // value (which forces a type rebuild from FLOAT → BOOL), then set the INT sentinel.
    Map* m = obj.map;
    TypeMap* map_type = m ? (TypeMap*)m->type : NULL;
    if (map_type && map_type->shape && get_type_id(key) == LMD_TYPE_STRING) {
        String* str_key = it2s(key);
        if (str_key) {
            ShapeEntry* entry = map_type->shape;
            while (entry) {
                if (entry->name && entry->name->length == (size_t)str_key->len &&
                    strncmp(entry->name->str, str_key->chars, str_key->len) == 0) {
                    if (entry->type->type_id == LMD_TYPE_FLOAT) {
                        // Force type change from FLOAT → BOOL via rebuild
                        js_property_set(obj, key, (Item){.item = b2it(false)});
                    }
                    break;
                }
                entry = entry->next;
            }
        }
    }
    // Phase 4 / A2-T3: clear IS_ACCESSOR shape flag before writing sentinel.
    // If the property was an accessor (slot holds JsAccessorPair*), the
    // sentinel write would leave IS_ACCESSOR set with a stale int sentinel
    // in the slot — subsequent reads (descriptor synthesis, dispatch) would
    // dereference the sentinel as a pair pointer and crash. Routed through
    // the per-Map clone primitive so sibling Maps are unaffected.
    if (map_type && map_type->shape && get_type_id(key) == LMD_TYPE_STRING) {
        String* str_key = it2s(key);
        if (str_key && str_key->len > 0) {
            ShapeEntry* _se = js_find_shape_entry(obj, str_key->chars, (int)str_key->len);
            if (_se && jspd_is_accessor(_se)) {
                js_shape_entry_set_accessor(obj, str_key->chars, (int)str_key->len, /*is_accessor=*/false);
            }
        }
    }
    js_property_set(obj, key, (Item){.item = JS_DELETED_SENTINEL_VAL});
    // A2-T8 dual-write: also stamp the JSPD_DELETED bit on the shape entry so
    // future readers can detect tombstones without consulting the slot value
    // (avoids the FLOAT-key sentinel-misread bug class). The slot sentinel
    // continues to be written for back-compat with un-migrated readers; once
    // all readers move to bit-only probing, this dual-write collapses to the
    // bit-only call. No-op when no shape entry exists for the key.
    if (map_type && map_type->shape && get_type_id(key) == LMD_TYPE_STRING) {
        String* str_key = it2s(key);
        if (str_key && str_key->len > 0) {
            js_shape_entry_set_deleted(obj, str_key->chars, (int)str_key->len, /*is_deleted=*/true);
        }
    }
    return (Item){.item = b2it(true)};
}

// =============================================================================
// v12: encodeURIComponent / decodeURIComponent / atob / btoa
// =============================================================================

// Base64 decoding table
static const unsigned char b64_decode_table[256] = {
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
     52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,  0,255,255,
    255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
     15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
    255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
     41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
};

// helper: throw DOMException with InvalidCharacterError
extern "C" Item js_domexception_new(Item message, Item name_arg);
static Item js_throw_domexception_invalid_char(const char* msg) {
    extern void js_throw_value(Item val);
    Item msg_item = (Item){.item = s2it(heap_create_name(msg, strlen(msg)))};
    Item name_item = (Item){.item = s2it(heap_create_name("InvalidCharacterError", 21))};
    Item ex = js_domexception_new(msg_item, name_item);
    js_throw_value(ex);
    return ItemNull;
}

extern "C" Item js_atob(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};

    const char* src = s->chars;
    int src_len = s->len;

    // Step 1: remove ASCII whitespace from data
    char* cleaned = (char*)mem_alloc(src_len + 1, MEM_CAT_JS_RUNTIME);
    if (!cleaned) return (Item){.item = s2it(heap_create_name("", 0))};
    int clen = 0;
    for (int i = 0; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') continue;
        cleaned[clen++] = (char)c;
    }

    // Step 2: if length % 4 == 0, remove 1-2 trailing '='
    if (clen > 0 && cleaned[clen - 1] == '=') clen--;
    if (clen > 0 && cleaned[clen - 1] == '=') clen--;

    // Step 3: if length % 4 == 1, throw InvalidCharacterError
    if (clen % 4 == 1) {
        mem_free(cleaned);
        return js_throw_domexception_invalid_char("Invalid character");
    }

    // Step 4: validate all remaining characters are in base64 alphabet
    for (int i = 0; i < clen; i++) {
        unsigned char c = (unsigned char)cleaned[i];
        if (b64_decode_table[c] == 255) {
            mem_free(cleaned);
            return js_throw_domexception_invalid_char("Invalid character");
        }
    }

    // Decode
    char* buf = (char*)mem_alloc(clen, MEM_CAT_JS_RUNTIME);
    if (!buf) { mem_free(cleaned); return (Item){.item = s2it(heap_create_name("", 0))}; }
    int out = 0;
    int bits = 0;
    int val = 0;
    for (int i = 0; i < clen; i++) {
        unsigned char d = b64_decode_table[(unsigned char)cleaned[i]];
        val = (val << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            buf[out++] = (char)((val >> bits) & 0xFF);
        }
    }

    String* result = heap_create_name(buf, out);
    mem_free(buf);
    mem_free(cleaned);
    return (Item){.item = s2it(result)};
}

static const char b64_encode_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

extern "C" Item js_btoa(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};

    // Check for characters outside Latin1 range (> 0xFF)
    // In UTF-8, any byte >= 0xC4 followed by >= 0x80 means code point > 0xFF
    const unsigned char* src = (const unsigned char*)s->chars;
    int src_len = s->len;
    for (int i = 0; i < src_len; i++) {
        unsigned char c = src[i];
        if (c >= 0xC4 && i + 1 < src_len && src[i + 1] >= 0x80) {
            return js_throw_domexception_invalid_char(
                "The string to be encoded contains characters outside of the Latin1 range.");
        }
    }
    int out_len = ((src_len + 2) / 3) * 4;
    char* buf = (char*)mem_alloc(out_len + 1, MEM_CAT_JS_RUNTIME);
    if (!buf) return (Item){.item = s2it(heap_create_name("", 0))};

    int out = 0;
    int i = 0;
    while (i < src_len) {
        unsigned int b0 = src[i];
        unsigned int b1 = (i + 1 < src_len) ? src[i + 1] : 0;
        unsigned int b2 = (i + 2 < src_len) ? src[i + 2] : 0;
        int remaining = src_len - i;
        unsigned int triple = (b0 << 16) | (b1 << 8) | b2;
        buf[out++] = b64_encode_table[(triple >> 18) & 0x3F];
        buf[out++] = b64_encode_table[(triple >> 12) & 0x3F];
        buf[out++] = (remaining > 1) ? b64_encode_table[(triple >> 6) & 0x3F] : '=';
        buf[out++] = (remaining > 2) ? b64_encode_table[triple & 0x3F] : '=';
        i += 3;
    }

    String* result = heap_create_name(buf, out);
    mem_free(buf);
    return (Item){.item = s2it(result)};
}

// ES spec: encodeURI/encodeURIComponent must throw URIError for lone surrogates.
// In CESU-8 (how Lambda stores JS strings), surrogates appear as:
//   High surrogates U+D800-U+DBFF: ED A0 80 - ED AF BF
//   Low surrogates  U+DC00-U+DFFF: ED B0 80 - ED BF BF
// A valid pair is high followed immediately by low. Anything else is lone.
static bool js_has_lone_surrogate(const char* s, int len) {
    for (int i = 0; i < len; ) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0xED && i + 2 < len) {
            unsigned char b1 = (unsigned char)s[i + 1];
            if (b1 >= 0xA0 && b1 <= 0xAF) {
                // high surrogate — check for following low surrogate
                if (i + 5 < len && (unsigned char)s[i + 3] == 0xED) {
                    unsigned char nb1 = (unsigned char)s[i + 4];
                    if (nb1 >= 0xB0 && nb1 <= 0xBF) {
                        i += 6; // valid pair, skip both
                        continue;
                    }
                }
                return true; // lone high surrogate
            } else if (b1 >= 0xB0 && b1 <= 0xBF) {
                return true; // lone low surrogate (not preceded by high)
            }
            i += 3; // non-surrogate ED sequence
        } else if (c >= 0xF0) { i += 4; }
        else if (c >= 0xE0) { i += 3; }
        else if (c >= 0xC0) { i += 2; }
        else { i += 1; }
    }
    return false;
}

static Item js_throw_uri_error(const char* msg) {
    Item tn = (Item){.item = s2it(heap_create_name("URIError", 8))};
    Item m  = (Item){.item = s2it(heap_create_name(msg, strlen(msg)))};
    extern Item js_new_error_with_name(Item type_name, Item message);
    js_throw_value(js_new_error_with_name(tn, m));
    return ItemNull;
}

extern "C" Item js_encodeURIComponent(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};
    // ES spec: throw URIError for lone surrogates
    if (js_has_lone_surrogate(s->chars, s->len)) {
        return js_throw_uri_error("URI malformed");
    }
    char* encoded = url_encode_component(s->chars, s->len);
    if (!encoded) return (Item){.item = s2it(heap_create_name("", 0))};
    String* result = heap_create_name(encoded, strlen(encoded));
    mem_free(encoded); // from url_encode_* in lib/url.c - raw malloc;
    return (Item){.item = s2it(result)};
}

extern "C" uint64_t js_get_heap_epoch();

extern "C" Item js_decodeURIComponent(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};
    size_t decoded_len = 0;
    char* decoded = url_decode_component(s->chars, s->len, &decoded_len);
    if (!decoded) {
        // Cache URIError object per-epoch to avoid expensive error creation
        // in hot loops (e.g., test262 tests that iterate 65000+ code points).
        static Item cached_error = {0};
        static uint64_t cached_epoch = 0;
        if (!cached_error.item || cached_epoch != js_get_heap_epoch()) {
            Item tn = (Item){.item = s2it(heap_create_name("URIError", 8))};
            Item msg = (Item){.item = s2it(heap_create_name("URI malformed", 13))};
            extern Item js_new_error_with_name(Item type_name, Item message);
            cached_error = js_new_error_with_name(tn, msg);
            cached_epoch = js_get_heap_epoch();
        }
        js_throw_value(cached_error);
        return ItemNull;
    }
    String* result = heap_create_name(decoded, decoded_len);
    mem_free(decoded); // from url_decode_* in lib/url.c - raw malloc;
    return (Item){.item = s2it(result)};
}

// v20: encodeURI / decodeURI (non-Component variants preserving URI structural chars)
extern "C" Item js_encodeURI(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};
    // ES spec: throw URIError for lone surrogates
    if (js_has_lone_surrogate(s->chars, s->len)) {
        return js_throw_uri_error("URI malformed");
    }
    char* encoded = url_encode_uri(s->chars, s->len);
    if (!encoded) return (Item){.item = s2it(heap_create_name("", 0))};
    String* result = heap_create_name(encoded, strlen(encoded));
    mem_free(encoded); // from url_encode_* in lib/url.c - raw malloc;
    return (Item){.item = s2it(result)};
}

extern "C" Item js_decodeURI(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};
    size_t decoded_len = 0;
    char* decoded = url_decode_uri(s->chars, s->len, &decoded_len);
    if (!decoded) {
        static Item cached_error = {0};
        static uint64_t cached_epoch = 0;
        if (!cached_error.item || cached_epoch != js_get_heap_epoch()) {
            Item tn = (Item){.item = s2it(heap_create_name("URIError", 8))};
            Item msg = (Item){.item = s2it(heap_create_name("URI malformed", 13))};
            extern Item js_new_error_with_name(Item type_name, Item message);
            cached_error = js_new_error_with_name(tn, msg);
            cached_epoch = js_get_heap_epoch();
        }
        js_throw_value(cached_error);
        return ItemNull;
    }
    String* result = heap_create_name(decoded, decoded_len);
    mem_free(decoded); // from url_decode_* in lib/url.c - raw malloc;
    return (Item){.item = s2it(result)};
}

// =============================================================================
// unescape(string) — legacy percent-decoding (%XX and %uXXXX)
// =============================================================================

static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

extern "C" Item js_unescape(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};

    const char* src = s->chars;
    int src_len = s->len;

    // allocate output buffer (worst case: all %XX with values >= 0x80 → 2 bytes each,
    // but that's still ≤ src_len since 3 input bytes → 2 output bytes)
    char* buf = (char*)mem_alloc(src_len * 2 + 1, MEM_CAT_JS_RUNTIME);
    if (!buf) return (Item){.item = s2it(heap_create_name("", 0))};

    int out = 0;
    int i = 0;
    while (i < src_len) {
        if (src[i] == '%' && i + 2 < src_len) {
            if (src[i + 1] == 'u' && i + 5 < src_len) {
                // %uXXXX
                int d0 = hex_digit_value(src[i + 2]);
                int d1 = hex_digit_value(src[i + 3]);
                int d2 = hex_digit_value(src[i + 4]);
                int d3 = hex_digit_value(src[i + 5]);
                if (d0 >= 0 && d1 >= 0 && d2 >= 0 && d3 >= 0) {
                    int cp = (d0 << 12) | (d1 << 8) | (d2 << 4) | d3;
                    if (cp <= 0x7F) {
                        buf[out++] = (char)cp;
                    } else if (cp <= 0x7FF) {
                        buf[out++] = (char)(0xC0 | (cp >> 6));
                        buf[out++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        buf[out++] = (char)(0xE0 | (cp >> 12));
                        buf[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        buf[out++] = (char)(0x80 | (cp & 0x3F));
                    }
                    i += 6;
                    continue;
                }
            }
            // %XX
            int d0 = hex_digit_value(src[i + 1]);
            int d1 = hex_digit_value(src[i + 2]);
            if (d0 >= 0 && d1 >= 0) {
                int cp = (d0 << 4) | d1;
                if (cp <= 0x7F) {
                    buf[out++] = (char)cp;
                } else {
                    // UTF-8 encode values 0x80-0xFF as 2-byte sequences
                    buf[out++] = (char)(0xC0 | (cp >> 6));
                    buf[out++] = (char)(0x80 | (cp & 0x3F));
                }
                i += 3;
                continue;
            }
        }
        buf[out++] = src[i++];
    }

    String* result = heap_create_name(buf, out);
    mem_free(buf);
    return (Item){.item = s2it(result)};
}

// =============================================================================
// escape(string) — legacy percent-encoding (%XX and %uXXXX)
// Characters NOT escaped: A-Z a-z 0-9 @ * _ + - . /
// =============================================================================

static bool js_escape_is_passthrough(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= 'a' && c <= 'z') return true;
    if (c >= '0' && c <= '9') return true;
    // @*_+-./
    return c == '@' || c == '*' || c == '_' || c == '+' || c == '-' || c == '.' || c == '/';
}

extern "C" Item js_escape(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};

    const char* src = s->chars;
    int src_len = s->len;

    // worst case: every char becomes %uXXXX (6 bytes per input byte)
    char* buf = (char*)mem_alloc(src_len * 6 + 1, MEM_CAT_JS_RUNTIME);
    if (!buf) return (Item){.item = s2it(heap_create_name("", 0))};

    static const char hex[] = "0123456789ABCDEF";
    int out = 0;
    int i = 0;
    while (i < src_len) {
        unsigned char c = (unsigned char)src[i];

        if (js_escape_is_passthrough(c)) {
            buf[out++] = (char)c;
            i++;
            continue;
        }

        // decode UTF-8 codepoint
        uint32_t cp;
        int bytes;
        if (c < 0x80) {
            cp = c; bytes = 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < src_len) {
            cp = (c & 0x1F) << 6 | ((unsigned char)src[i+1] & 0x3F);
            bytes = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < src_len) {
            cp = (c & 0x0F) << 12 | ((unsigned char)src[i+1] & 0x3F) << 6 | ((unsigned char)src[i+2] & 0x3F);
            bytes = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < src_len) {
            // surrogate pair for codepoints above 0xFFFF
            cp = (c & 0x07) << 18 | ((unsigned char)src[i+1] & 0x3F) << 12 | ((unsigned char)src[i+2] & 0x3F) << 6 | ((unsigned char)src[i+3] & 0x3F);
            bytes = 4;
        } else {
            cp = c; bytes = 1;
        }

        if (cp > 0xFFFF) {
            // encode as surrogate pair: %uD800-style
            uint32_t hi = 0xD800 + ((cp - 0x10000) >> 10);
            uint32_t lo = 0xDC00 + ((cp - 0x10000) & 0x3FF);
            buf[out++] = '%'; buf[out++] = 'u';
            buf[out++] = hex[(hi >> 12) & 0xF]; buf[out++] = hex[(hi >> 8) & 0xF];
            buf[out++] = hex[(hi >> 4) & 0xF]; buf[out++] = hex[hi & 0xF];
            buf[out++] = '%'; buf[out++] = 'u';
            buf[out++] = hex[(lo >> 12) & 0xF]; buf[out++] = hex[(lo >> 8) & 0xF];
            buf[out++] = hex[(lo >> 4) & 0xF]; buf[out++] = hex[lo & 0xF];
        } else if (cp > 0xFF) {
            // %uXXXX
            buf[out++] = '%'; buf[out++] = 'u';
            buf[out++] = hex[(cp >> 12) & 0xF]; buf[out++] = hex[(cp >> 8) & 0xF];
            buf[out++] = hex[(cp >> 4) & 0xF]; buf[out++] = hex[cp & 0xF];
        } else {
            // %XX
            buf[out++] = '%';
            buf[out++] = hex[(cp >> 4) & 0xF];
            buf[out++] = hex[cp & 0xF];
        }
        i += bytes;
    }

    String* result = heap_create_name(buf, out);
    mem_free(buf);
    return (Item){.item = s2it(result)};
}

// =============================================================================
// v12: globalThis
// =============================================================================

static Item js_global_this_obj = {0};

/**
 * Reset globalThis for batch mode. Forces re-creation on next access
 * so element IDs and variables from previous files don't leak.
 */
extern "C" void js_globals_batch_reset() {
    js_global_this_obj = (Item){0};
    // reset constructor cache (function objects from old pool)
    extern void js_ctor_cache_reset();
    js_ctor_cache_reset();
    // reset global builtin function cache (JsFunctionLayout* in old pool)
    extern void js_global_builtin_fn_cache_reset();
    js_global_builtin_fn_cache_reset();
    // reset process.argv cache and process object
    js_process_argv_items = (Item){.item = ITEM_NULL};
    js_process_object = (Item){.item = ITEM_NULL};
    js_process_argc_raw = 0;
    js_process_argv_raw = NULL;
    // reset with-statement scope stack — stale Items become dangling after heap reset
    extern void js_with_batch_reset(void);
    js_with_batch_reset();
    // reset GeneratorFunction.prototype caches — objects live in old heap after reset
    js_generator_function_proto_cache = (Item){0};
    js_async_generator_function_proto_cache = (Item){0};
    js_async_function_proto_cache = (Item){0};
}

// =============================================================================
// AbortController / AbortSignal implementation
// =============================================================================

static Item make_string_item(const char* str) {
    return (Item){.item = s2it(heap_create_name(str, strlen(str)))};
}
static Item make_string_item(const char* str, int len) {
    return (Item){.item = s2it(heap_create_name(str, len))};
}

// AbortSignal constructor — creates an AbortSignal object
static Item js_make_abort_signal() {
    Item signal = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(signal, JS_CLASS_ABORT_SIGNAL);  // A3-T3b
    js_property_set(signal, make_string_item("aborted"), (Item){.item = b2it(false)});
    js_property_set(signal, make_string_item("reason"), make_js_undefined());
    js_property_set(signal, make_string_item("__listeners__"), js_array_new(0));
    return signal;
}

// signal.addEventListener / signal.on
extern "C" Item js_abort_signal_addEventListener(Item event, Item handler) {
    Item self = js_get_this();
    // store in __listeners__ array
    Item listeners = js_property_get(self, make_string_item("__listeners__"));
    if (get_type_id(listeners) == LMD_TYPE_ARRAY) {
        Item entry = js_new_object();
        js_property_set(entry, make_string_item("type"), event);
        js_property_set(entry, make_string_item("handler"), handler);
        js_array_push(listeners, entry);
    }
    return make_js_undefined();
}

// signal.removeEventListener
extern "C" Item js_abort_signal_removeEventListener(Item event, Item handler) {
    return make_js_undefined();
}

// signal.throwIfAborted()
extern "C" Item js_abort_signal_throwIfAborted(void) {
    Item self = js_get_this();
    Item aborted = js_property_get(self, make_string_item("aborted"));
    if (get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted)) {
        Item reason = js_property_get(self, make_string_item("reason"));
        return reason; // caller should throw this
    }
    return make_js_undefined();
}

// AbortSignal.abort(reason) — creates an already-aborted signal
extern "C" Item js_abort_signal_abort(Item reason) {
    Item signal = js_make_abort_signal();
    // set methods on the signal
    js_property_set(signal, make_string_item("addEventListener"),
        js_new_function((void*)js_abort_signal_addEventListener, 2));
    js_property_set(signal, make_string_item("removeEventListener"),
        js_new_function((void*)js_abort_signal_removeEventListener, 2));
    js_property_set(signal, make_string_item("throwIfAborted"),
        js_new_function((void*)js_abort_signal_throwIfAborted, 0));
    js_property_set(signal, make_string_item("onabort"), ItemNull);
    // mark as already aborted
    js_property_set(signal, make_string_item("aborted"), (Item){.item = b2it(true)});
    // default reason: DOMException "AbortError"
    if (get_type_id(reason) == LMD_TYPE_UNDEFINED || get_type_id(reason) == LMD_TYPE_NULL) {
        Item err = js_new_object();
        // T5b: legacy `__class_name__` string write retired.
        js_class_stamp(err, JS_CLASS_DOM_EXCEPTION);  // A3-T3b
        js_property_set(err, make_string_item("name"), make_string_item("AbortError"));
        js_property_set(err, make_string_item("message"), make_string_item("This operation was aborted"));
        js_property_set(err, make_string_item("code"), (Item){.item = i2it(20)});
        reason = err;
    }
    js_property_set(signal, make_string_item("reason"), reason);
    return signal;
}

// AbortSignal.timeout(ms) — creates a signal that auto-aborts after ms
extern "C" Item js_abort_signal_timeout(Item ms_item) {
    // create signal (not yet aborted)
    Item signal = js_make_abort_signal();
    js_property_set(signal, make_string_item("addEventListener"),
        js_new_function((void*)js_abort_signal_addEventListener, 2));
    js_property_set(signal, make_string_item("removeEventListener"),
        js_new_function((void*)js_abort_signal_removeEventListener, 2));
    js_property_set(signal, make_string_item("throwIfAborted"),
        js_new_function((void*)js_abort_signal_throwIfAborted, 0));
    js_property_set(signal, make_string_item("onabort"), ItemNull);
    // TODO: actually schedule a timeout to abort after ms
    // for now just return the un-aborted signal
    return signal;
}

// generic stub constructor — returns a new empty object
extern "C" Item js_stub_constructor(Item arg) {
    (void)arg;
    return js_new_object();
}

// Option(text, value, defaultSelected, selected) — HTMLOptionElement constructor stub
extern "C" Item js_option_new(Item text_arg, Item value_arg) {
    // create a minimal option object
    Item obj = js_new_object();
    const char* text = fn_to_cstr(text_arg);
    const char* val  = fn_to_cstr(value_arg);
    if (text) js_property_set(obj, make_string_item("text"),  make_string_item(text));
    if (val)  js_property_set(obj, make_string_item("value"), make_string_item(val));
    return obj;
}

// DOMException(message, nameOrOptions) constructor
extern "C" Item js_domexception_new(Item message, Item name_arg) {
    Item obj = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(obj, JS_CLASS_DOM_EXCEPTION);  // A3-T3b

    // message (default: "")
    if (get_type_id(message) == LMD_TYPE_STRING) {
        js_property_set(obj, make_string_item("message"), message);
    } else {
        js_property_set(obj, make_string_item("message"), make_string_item(""));
    }

    // name can be a string or an options object { name, cause }
    Item actual_name = make_string_item("Error");
    bool has_cause = false;
    Item cause_val = make_js_undefined();

    TypeId name_type = get_type_id(name_arg);
    if (name_type == LMD_TYPE_STRING) {
        actual_name = name_arg;
    } else if (name_type == LMD_TYPE_MAP || name_type == LMD_TYPE_OBJECT) {
        // options object: { name: "...", cause: ... }
        Item name_prop = js_property_get(name_arg, make_string_item("name"));
        if (get_type_id(name_prop) == LMD_TYPE_STRING) {
            actual_name = name_prop;
        }
        // check if 'cause' is an own property
        extern Item js_has_own_property(Item obj, Item key);
        Item has_cause_item = js_has_own_property(name_arg, make_string_item("cause"));
        if (get_type_id(has_cause_item) == LMD_TYPE_BOOL && it2b(has_cause_item)) {
            has_cause = true;
            cause_val = js_property_get(name_arg, make_string_item("cause"));
        }
    }

    js_property_set(obj, make_string_item("name"), actual_name);

    // set cause if present
    if (has_cause) {
        js_property_set(obj, make_string_item("cause"), cause_val);
    }

    // DOMException legacy code mappings
    int code = 0;
    if (get_type_id(actual_name) == LMD_TYPE_STRING) {
        String* ns = it2s(actual_name);
        if (ns) {
            struct { const char* name; int code; } codes[] = {
                {"IndexSizeError", 1}, {"HierarchyRequestError", 3},
                {"WrongDocumentError", 4}, {"InvalidCharacterError", 5},
                {"NoModificationAllowedError", 7}, {"NotFoundError", 8},
                {"NotSupportedError", 9}, {"InvalidStateError", 11},
                {"SyntaxError", 12}, {"InvalidModificationError", 13},
                {"NamespaceError", 14}, {"InvalidAccessError", 15},
                {"TypeMismatchError", 17}, {"SecurityError", 18},
                {"NetworkError", 19}, {"AbortError", 20},
                {"URLMismatchError", 21}, {"QuotaExceededError", 22},
                {"TimeoutError", 23}, {"InvalidNodeTypeError", 24},
                {"DataCloneError", 25}, {NULL, 0}
            };
            for (int i = 0; codes[i].name; i++) {
                if (ns->len == strlen(codes[i].name) && strncmp(ns->chars, codes[i].name, ns->len) == 0) {
                    code = codes[i].code;
                    break;
                }
            }
        }
    }
    js_property_set(obj, make_string_item("code"), (Item){.item = i2it(code)});

    // stack property (empty for DOMException)
    js_property_set(obj, make_string_item("stack"), make_string_item(""));

    return obj;
}

// forward declaration
extern "C" Item js_abort_controller_abort(Item reason);

// AbortController() constructor
extern "C" Item js_new_AbortController(void) {
    Item controller = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(controller, JS_CLASS_ABORT_CONTROLLER);  // A3-T3b

    Item signal = js_make_abort_signal();
    // set signal methods
    js_property_set(signal, make_string_item("addEventListener"),
        js_new_function((void*)js_abort_signal_addEventListener, 2));
    js_property_set(signal, make_string_item("removeEventListener"),
        js_new_function((void*)js_abort_signal_removeEventListener, 2));
    js_property_set(signal, make_string_item("throwIfAborted"),
        js_new_function((void*)js_abort_signal_throwIfAborted, 0));
    js_property_set(signal, make_string_item("onabort"), ItemNull);
    js_property_set(controller, make_string_item("signal"), signal);

    // abort method directly on instance
    js_property_set(controller, make_string_item("abort"),
        js_new_function((void*)js_abort_controller_abort, 1));

    return controller;
}

// AbortController.prototype.abort(reason)
extern "C" Item js_abort_controller_abort(Item reason) {
    Item self = js_get_this();
    Item signal = js_property_get(self, make_string_item("signal"));
    if (get_type_id(signal) != LMD_TYPE_MAP) return make_js_undefined();

    // no-op if already aborted
    Item already = js_property_get(signal, make_string_item("aborted"));
    if (get_type_id(already) == LMD_TYPE_BOOL && it2b(already))
        return make_js_undefined();

    // mark as aborted
    js_property_set(signal, make_string_item("aborted"), (Item){.item = b2it(true)});

    // set reason (default: DOMException "AbortError")
    if (get_type_id(reason) == LMD_TYPE_NULL || get_type_id(reason) == LMD_TYPE_UNDEFINED) {
        Item err = js_new_object();
        js_property_set(err, make_string_item("name"), make_string_item("AbortError"));
        js_property_set(err, make_string_item("message"), make_string_item("The operation was aborted"));
        js_property_set(err, make_string_item("code"), (Item){.item = i2it(20)});
        reason = err;
    }
    js_property_set(signal, make_string_item("reason"), reason);

    // create abort event once, shared by onabort and addEventListener handlers
    extern Item js_call_function(Item func, Item this_val, Item* args, int nargs);
    Item event = js_new_object();
    js_property_set(event, make_string_item("type"), make_string_item("abort"));
    js_property_set(event, make_string_item("target"), signal);
    js_property_set(event, make_string_item("isTrusted"), (Item){.item = b2it(true)});

    // fire onabort handler
    Item onabort = js_property_get(signal, make_string_item("onabort"));
    if (get_type_id(onabort) == LMD_TYPE_FUNC) {
        Item argv[1] = { event };
        js_call_function(onabort, signal, argv, 1);
    }

    // fire 'abort' event listeners
    Item listeners = js_property_get(signal, make_string_item("__listeners__"));
    if (get_type_id(listeners) == LMD_TYPE_ARRAY) {
        extern Item js_call_function(Item func, Item this_val, Item* args, int nargs);
        extern void js_clearTimeout(Item timer_id);
        int64_t len = js_array_length(listeners);
        for (int i = 0; i < (int)len; i++) {
            Item entry = js_array_get_int(listeners, i);
            Item type = js_property_get(entry, make_string_item("type"));
            if (get_type_id(type) == LMD_TYPE_STRING) {
                String* ts = it2s(type);
                if (ts->len == 5 && memcmp(ts->chars, "abort", 5) == 0) {
                    // check for timer promise reject entry
                    Item timer_reject = js_property_get(entry, make_string_item("__timer_reject__"));
                    if (get_type_id(timer_reject) == LMD_TYPE_FUNC) {
                        // reject promise with AbortError and clear the timer
                        Item timer_signal = js_property_get(entry, make_string_item("__timer_signal__"));
                        Item abort_err = js_new_object();
                        // T5b: legacy `__class_name__` string write retired.
                        js_class_stamp(abort_err, JS_CLASS_ABORT_ERROR);  // A3-T3b
                        js_property_set(abort_err, make_string_item("name"), make_string_item("AbortError"));
                        js_property_set(abort_err, make_string_item("code"), make_string_item("ABORT_ERR"));
                        js_property_set(abort_err, make_string_item("message"), make_string_item("The operation was aborted"));
                        // propagate cause from signal reason
                        if (get_type_id(timer_signal) == LMD_TYPE_MAP) {
                            Item sig_reason = js_property_get(timer_signal, make_string_item("reason"));
                            if (get_type_id(sig_reason) != LMD_TYPE_UNDEFINED && get_type_id(sig_reason) != LMD_TYPE_NULL) {
                                js_property_set(abort_err, make_string_item("cause"), sig_reason);
                            }
                        }
                        Item argv[1] = { abort_err };
                        js_call_function(timer_reject, ItemNull, argv, 1);
                        // clear the associated timer
                        Item timer_id = js_property_get(entry, make_string_item("__timer_id__"));
                        if (get_type_id(timer_id) == LMD_TYPE_INT) {
                            js_clearTimeout(timer_id);
                        }
                        continue;
                    }
                    Item handler = js_property_get(entry, make_string_item("handler"));
                    if (get_type_id(handler) == LMD_TYPE_FUNC) {
                        Item argv[1] = { event };
                        js_call_function(handler, signal, argv, 1);
                    }
                }
            }
        }
    }
    return make_js_undefined();
}

// =============================================================================
// MessagePort / MessageChannel stubs
// =============================================================================

static Item js_mp_stub_noop(void) {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static Item js_message_port_postMessage(Item msg) {
    (void)msg;
    // stub: no cross-context messaging
    return make_js_undefined();
}

static Item js_message_port_close(void) {
    return make_js_undefined();
}

extern "C" Item js_message_port_new(void) {
    Item port = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(port, JS_CLASS_MESSAGE_PORT);  // A3-T3b
    js_property_set(port, make_string_item("postMessage"),
        js_new_function((void*)js_message_port_postMessage, 1));
    js_property_set(port, make_string_item("close"),
        js_new_function((void*)js_message_port_close, 0));
    js_property_set(port, make_string_item("onmessage"), ItemNull);
    js_property_set(port, make_string_item("onmessageerror"), ItemNull);
    // EventEmitter methods
    js_property_set(port, make_string_item("on"),
        js_new_function((void*)js_mp_stub_noop, 2));
    js_property_set(port, make_string_item("once"),
        js_new_function((void*)js_mp_stub_noop, 2));
    js_property_set(port, make_string_item("addEventListener"),
        js_new_function((void*)js_mp_stub_noop, 2));
    js_property_set(port, make_string_item("removeEventListener"),
        js_new_function((void*)js_mp_stub_noop, 2));
    js_property_set(port, make_string_item("start"),
        js_new_function((void*)js_mp_stub_noop, 0));
    js_property_set(port, make_string_item("ref"),
        js_new_function((void*)js_mp_stub_noop, 0));
    js_property_set(port, make_string_item("unref"),
        js_new_function((void*)js_mp_stub_noop, 0));
    return port;
}

extern "C" Item js_message_channel_new(void) {
    Item channel = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(channel, JS_CLASS_MESSAGE_CHANNEL);  // A3-T3b
    Item port1 = js_message_port_new();
    Item port2 = js_message_port_new();
    js_property_set(channel, make_string_item("port1"), port1);
    js_property_set(channel, make_string_item("port2"), port2);
    return channel;
}

// forward declaration for populating globalThis with constructors
extern "C" Item js_get_constructor(Item name_item);

extern "C" Item js_get_global_this() {
    if (js_global_this_obj.item == 0) {
        js_global_this_obj = js_new_object();
        // populate standard globals
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("undefined", 9))}, make_js_undefined());
        // Legacy IE-style `window.event` — initially undefined, set to the
        // in-flight event during dispatch by js_dom_dispatch_event.
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("event", 5))}, make_js_undefined());
        double* nan_p = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *nan_p = NAN;
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("NaN", 3))}, (Item){.item = d2it(nan_p)});
        double* inf_p = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *inf_p = INFINITY;
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("Infinity", 8))}, (Item){.item = d2it(inf_p)});

        // ES spec: NaN, Infinity, undefined are non-writable, non-enumerable, non-configurable
        static const char* ro_globals[] = {"NaN", "Infinity", "undefined", NULL};
        for (int i = 0; ro_globals[i]; i++) {
            int nlen = (int)strlen(ro_globals[i]);
            Item key = (Item){.item = s2it(heap_create_name(ro_globals[i], nlen))};
            js_mark_non_enumerable(js_global_this_obj, key);
            js_mark_non_writable(js_global_this_obj, key);
            js_mark_non_configurable(js_global_this_obj, key);
        }

        // populate constructor functions on globalThis
        static const struct { const char* name; int len; } ctor_names[] = {
            {"Object", 6}, {"Array", 5}, {"Function", 8},
            {"String", 6}, {"Number", 6}, {"Boolean", 7}, {"Symbol", 6},
            {"Error", 5}, {"TypeError", 9}, {"RangeError", 10},
            {"ReferenceError", 14}, {"SyntaxError", 11},
            {"URIError", 8}, {"EvalError", 9}, {"AggregateError", 14},
            {"RegExp", 6}, {"Date", 4}, {"Promise", 7},
            {"Map", 3}, {"Set", 3}, {"WeakMap", 7}, {"WeakSet", 7},
            {"ArrayBuffer", 11}, {"DataView", 8},
            {"Int8Array", 9}, {"Uint8Array", 10}, {"Uint8ClampedArray", 17},
            {"Int16Array", 10}, {"Uint16Array", 11},
            {"Int32Array", 10}, {"Uint32Array", 11},
            {"Float32Array", 12}, {"Float64Array", 12},
            {"BigInt64Array", 13}, {"BigUint64Array", 14},
            {"Proxy", 5},
            {"Event", 5}, {"CustomEvent", 11}, {"EventTarget", 11},
            {"UIEvent", 7}, {"FocusEvent", 10}, {"MouseEvent", 10},
            {"WheelEvent", 10}, {"KeyboardEvent", 13},
            {"CompositionEvent", 16}, {"InputEvent", 10}, {"PointerEvent", 12},
            {NULL, 0}
        };
        for (int i = 0; ctor_names[i].name; i++) {
            Item name_item = (Item){.item = s2it(heap_create_name(ctor_names[i].name, ctor_names[i].len))};
            Item ctor = js_get_constructor(name_item);
            if (get_type_id(ctor) == LMD_TYPE_FUNC) {
                js_property_set(js_global_this_obj, name_item, ctor);
            }
        }
        // globalThis self-reference
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("globalThis", 10))}, js_global_this_obj);
        // HTML / Web Workers spec aliases of the global object.
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("self", 4))}, js_global_this_obj);
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("window", 6))}, js_global_this_obj);

        // populate namespace objects on globalThis (Math, JSON, Reflect, console)
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("Math", 4))}, js_get_math_object_value());
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("JSON", 4))}, js_get_json_object_value());
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("Reflect", 7))}, js_get_reflect_object_value());
        extern Item js_get_atomics_object_value(void);
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("Atomics", 7))}, js_get_atomics_object_value());
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("console", 7))}, js_get_console_object_value());
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("process", 7))}, js_get_process_object_value());
        extern Item js_get_css_object_value(void);
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("CSS", 3))}, js_get_css_object_value());

        // populate global functions as own properties
        static const struct { const char* name; int len; int param_count; } global_fns[] = {
            {"parseInt", 8, 2}, {"parseFloat", 10, 1},
            {"isNaN", 5, 1}, {"isFinite", 8, 1},
            {"eval", 4, 1},
            {"decodeURI", 9, 1}, {"encodeURI", 9, 1},
            {"decodeURIComponent", 18, 1}, {"encodeURIComponent", 18, 1},
            {"escape", 6, 1}, {"unescape", 8, 1},
            // Timer and scheduling functions (Node.js globals)
            {"setTimeout", 10, 2}, {"setInterval", 11, 2},
            {"clearTimeout", 12, 1}, {"clearInterval", 13, 1},
            {"setImmediate", 12, 1}, {"clearImmediate", 14, 1},
            {"queueMicrotask", 14, 1},
            // Web API globals
            {"structuredClone", 15, 1}, {"fetch", 5, 2},
            {NULL, 0, 0}
        };
        for (int i = 0; global_fns[i].name; i++) {
            Item name_item = (Item){.item = s2it(heap_create_name(global_fns[i].name, global_fns[i].len))};
            Item fn = js_get_global_builtin_fn(name_item, (Item){.item = i2it(global_fns[i].param_count)});
            js_property_set(js_global_this_obj, name_item, fn);
        }

        // Node.js: 'global' is an alias for globalThis
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("global", 6))}, js_global_this_obj);

        // EventTarget interface methods on globalThis (window/self acts as
        // an EventTarget per HTML spec).
        {
            extern Item js_eventtarget_add_listener(Item, Item, Item);
            extern Item js_eventtarget_remove_listener(Item, Item, Item);
            extern Item js_eventtarget_dispatch(Item);
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("addEventListener", 16))},
                js_new_function((void*)js_eventtarget_add_listener, 3));
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("removeEventListener", 19))},
                js_new_function((void*)js_eventtarget_remove_listener, 3));
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("dispatchEvent", 13))},
                js_new_function((void*)js_eventtarget_dispatch, 1));
        }

        // Node.js: Buffer is a global
        extern Item js_get_buffer_namespace(void);
        js_property_set(js_global_this_obj,
            (Item){.item = s2it(heap_create_name("Buffer", 6))},
            js_get_buffer_namespace());

        // AbortController constructor
        {
            Item ac_ctor = js_new_function((void*)js_new_AbortController, 0);
            js_property_set(ac_ctor, make_string_item("prototype"), js_new_object());
            // abort method on instances (set by constructor), but also add as static for access
            Item ac_proto = js_property_get(ac_ctor, make_string_item("prototype"));
            js_property_set(ac_proto, make_string_item("abort"),
                js_new_function((void*)js_abort_controller_abort, 1));
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("AbortController", 15))},
                ac_ctor);
        }

        // AbortSignal — global with static methods abort() and timeout()
        {
            extern Item js_abort_signal_abort(Item reason);
            extern Item js_abort_signal_timeout(Item ms);
            Item as_ctor = js_new_function((void*)js_make_abort_signal, 0);
            js_property_set(as_ctor, make_string_item("abort"),
                js_new_function((void*)js_abort_signal_abort, 1));
            js_property_set(as_ctor, make_string_item("timeout"),
                js_new_function((void*)js_abort_signal_timeout, 1));
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("AbortSignal", 11))},
                as_ctor);
        }

        // TextEncoder / TextDecoder constructors as globals
        {
            extern Item js_text_encoder_new(void);
            extern Item js_text_decoder_new(Item encoding);
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("TextEncoder", 11))},
                js_new_function((void*)js_text_encoder_new, 0));
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("TextDecoder", 11))},
                js_new_function((void*)js_text_decoder_new, 1));
        }

        // globalThis.atob / globalThis.btoa
        {
            extern Item js_atob(Item);
            extern Item js_btoa(Item);
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("atob", 4))},
                js_new_function((void*)js_atob, 1));
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("btoa", 4))},
                js_new_function((void*)js_btoa, 1));
        }

        // globalThis.performance (basic stub with now())
        {
            extern Item js_performance_now(void);
            Item perf = js_new_object();
            js_property_set(perf, make_string_item("now"),
                js_new_function((void*)js_performance_now, 0));
            // timeOrigin: process start time (use 0 for simplicity)
            double* origin = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *origin = 0.0;
            js_property_set(perf, make_string_item("timeOrigin"), (Item){.item = d2it(origin)});
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("performance", 11))}, perf);
        }

        // globalThis.MessageChannel / MessagePort stubs
        {
            extern Item js_message_channel_new(void);
            extern Item js_message_port_new(void);
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("MessageChannel", 14))},
                js_new_function((void*)js_message_channel_new, 0));
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("MessagePort", 11))},
                js_new_function((void*)js_message_port_new, 0));
        }

        // globalThis.URLSearchParams
        {
            extern Item js_url_search_params_new(Item init);
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("URLSearchParams", 15))},
                js_new_function((void*)js_url_search_params_new, 1));
        }

        // globalThis.URL constructor
        {
            extern Item js_url_module_construct(Item input, Item base);
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("URL", 3))},
                js_new_function((void*)js_url_module_construct, 2));
        }

        // globalThis.DOMException constructor
        {
            extern Item js_domexception_new(Item message, Item name);
            Item ctor = js_new_function((void*)js_domexception_new, 2);
            Item proto = js_new_object();
            js_property_set(proto, make_string_item("constructor"), ctor);
            js_property_set(ctor, make_string_item("prototype"), proto);
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("DOMException", 12))}, ctor);
        }

        // globalThis.Option constructor (HTMLOptionElement)
        {
            extern Item js_option_new(Item text, Item value);
            js_property_set(js_global_this_obj,
                (Item){.item = s2it(heap_create_name("Option", 6))},
                js_new_function((void*)js_option_new, 2));
        }

        // Web Clipboard / Blob / File / ClipboardItem / ClipboardEvent /
        // navigator.clipboard / navigator.permissions
        {
            extern void js_register_clipboard_globals(Item global_this);
            js_register_clipboard_globals(js_global_this_obj);
        }

        // ES spec: all standard global properties are non-enumerable
        js_mark_all_non_enumerable(js_global_this_obj);
    }
    return js_global_this_obj;
}

// js_get_global_object: alias for js_get_global_this (used by assignment fallback)
extern "C" Item js_get_global_object() {
    return js_get_global_this();
}

// ============================================================================
// With-scope stack for 'with' statement support
// ============================================================================
#define JS_WITH_STACK_MAX 16
static Item js_with_stack[JS_WITH_STACK_MAX];
static int js_with_stack_depth = 0;

extern "C" void js_with_batch_reset(void) {
    js_with_stack_depth = 0;
    memset(js_with_stack, 0, sizeof(js_with_stack));
}

extern "C" void js_with_push(Item obj) {
    if (js_with_stack_depth < JS_WITH_STACK_MAX) {
        js_with_stack[js_with_stack_depth++] = obj;
    }
}

extern "C" void js_with_pop() {
    if (js_with_stack_depth > 0) {
        js_with_stack_depth--;
    }
}

extern "C" int js_with_save_depth() {
    return js_with_stack_depth;
}

extern "C" void js_with_restore_depth(int depth) {
    js_with_stack_depth = depth;
}

// Check with-scope stack for a property (most recent scope first)
static Item js_with_scope_lookup(Item key, bool* found) {
    extern int js_check_exception(void);
    *found = false;
    for (int i = js_with_stack_depth - 1; i >= 0; i--) {
        Item scope_obj = js_with_stack[i];
        if (get_type_id(scope_obj) == LMD_TYPE_MAP) {
            extern Item js_has_own_property(Item obj, Item key);
            if (it2b(js_has_own_property(scope_obj, key))) {
                // ES2023 9.1.1.2.1 step 6-9: check @@unscopables
                Item unscopables_sym = (Item){.item = i2it(-(int64_t)(11 + JS_SYMBOL_BASE))}; // Symbol.unscopables
                Item unscopables = js_property_get(scope_obj, unscopables_sym);
                if (js_check_exception()) {
                    *found = true;
                    return ItemNull; // getter threw — propagate
                }
                if (get_type_id(unscopables) == LMD_TYPE_MAP) {
                    Item blocked = js_property_get(unscopables, key);
                    if (js_check_exception()) {
                        *found = true;
                        return ItemNull;
                    }
                    extern bool js_is_truthy(Item value);
                    if (js_is_truthy(blocked)) {
                        continue; // binding is blocked by @@unscopables
                    }
                }
                *found = true;
                return js_property_get(scope_obj, key);
            }
        }
    }
    return make_js_undefined();
}

// js_get_global_property: look up a property on the global object by name string
// Used as fallback for unresolved identifiers — implements browser-like named access
extern "C" Item js_get_global_property(Item key) {
    // Check with-scope stack first
    if (js_with_stack_depth > 0) {
        bool found = false;
        Item result = js_with_scope_lookup(key, &found);
        if (found) return result;
    }
    Item global = js_get_global_this();
    return js_property_get(global, key);
}

// js_get_global_property_strict: like js_get_global_property but throws ReferenceError
// for properties that don't exist on the global object. Used for bare identifier reads
// (e.g. `x` as opposed to `obj.x`), which per ES spec must throw ReferenceError.
extern "C" Item js_get_global_property_strict(Item key) {
    // Check with-scope stack first
    if (js_with_stack_depth > 0) {
        bool found = false;
        Item result = js_with_scope_lookup(key, &found);
        if (found) return result;
    }
    Item global = js_get_global_this();
    Item result = js_property_get(global, key);
    // property_get returns JS undefined for missing keys.
    // We need to distinguish "property exists with value undefined" from "not found".
    if (get_type_id(result) == LMD_TYPE_UNDEFINED) {
        // Check if the property actually exists on the global (own or prototype chain)
        extern Item js_has_own_property(Item obj, Item key);
        if (!it2b(js_has_own_property(global, key))) {
            String* sk = it2s(key);
            if (sk) {
                char msg[256];
                snprintf(msg, sizeof(msg), "%.*s is not defined", (int)sk->len, sk->chars);
                extern void js_throw_reference_error(Item message);
                js_throw_reference_error((Item){.item = s2it(heap_create_name(msg, strlen(msg)))});
            }
        }
    }
    return result;
}

// js_set_global_property: write a property to the global object by name string
// Used for implicit global assignments (sloppy mode: assigning to undeclared variables)
extern "C" void js_set_global_property(Item key, Item value) {
    // Check with-scope stack first — assignments inside 'with' resolve to scope object
    if (js_with_stack_depth > 0) {
        for (int i = js_with_stack_depth - 1; i >= 0; i--) {
            Item scope_obj = js_with_stack[i];
            if (get_type_id(scope_obj) == LMD_TYPE_MAP) {
                extern Item js_has_own_property(Item obj, Item key);
                if (it2b(js_has_own_property(scope_obj, key))) {
                    js_property_set(scope_obj, key, value);
                    return;
                }
            }
        }
    }
    Item global = js_get_global_this();
    js_property_set(global, key, value);
}

// v48: Return a function wrapper for global builtins (parseInt, parseFloat, etc.)
// so they can be passed as values, and have .name/.length properties.
// Uses a simple cache indexed by name hash to avoid re-creating function objects.
#define GLOBAL_BUILTIN_CACHE_SIZE 32
static Item global_builtin_fn_cache[GLOBAL_BUILTIN_CACHE_SIZE];
static bool global_builtin_fn_cache_init = false;

void js_global_builtin_fn_cache_reset() {
    global_builtin_fn_cache_init = false;
}

extern "C" Item js_get_global_builtin_fn(Item name_item, Item param_count_item) {
    if (!global_builtin_fn_cache_init) {
        for (int i = 0; i < GLOBAL_BUILTIN_CACHE_SIZE; i++) global_builtin_fn_cache[i] = ItemNull;
        global_builtin_fn_cache_init = true;
    }
    String* name = it2s(name_item);
    if (!name) return ItemNull;
    int pc = (int)it2i(param_count_item);

    // Simple hash to cache slot
    unsigned hash = 0;
    for (int i = 0; i < (int)name->len; i++) hash = hash * 31 + (unsigned char)name->chars[i];
    int slot = hash % GLOBAL_BUILTIN_CACHE_SIZE;
    if (global_builtin_fn_cache[slot].item != ItemNull.item) {
        // verify name matches
        JsFunctionLayout* cached = (JsFunctionLayout*)global_builtin_fn_cache[slot].function;
        if (cached && cached->name && cached->name->len == name->len &&
            strncmp(cached->name->chars, name->chars, name->len) == 0) {
            return global_builtin_fn_cache[slot];
        }
    }

    // Create a new function object with builtin_id = -2 (dispatched by name at call site)
    // pool_calloc zeros all fields; don't set properties_map or prototype to ItemNull
    // since ItemNull.item != 0, which would confuse property lookup code.
    JsFunctionLayout* fn = (JsFunctionLayout*)pool_calloc(js_input->pool, sizeof(JsFunctionLayout));
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = NULL;
    fn->param_count = pc;
    fn->formal_length = -1; // -1 = use param_count for .length
    fn->builtin_id = -2; // marker: global builtin wrapper
    fn->name = heap_create_name(name->chars, name->len);
    // prototype and properties_map left as zero (pool_calloc)
    Item result = {.function = (Function*)fn};
    global_builtin_fn_cache[slot] = result;
    return result;
}

// =============================================================================
// Built-in constructor cache: Array, Object, Function, String, Number, Boolean, etc.
// These return JsFunction objects so that `typeof Array === "function"` and
// `Array.prototype.push` work correctly.
// =============================================================================

// Constructor IDs for dispatch
enum JsConstructorId {
    JS_CTOR_OBJECT = 1,
    JS_CTOR_ARRAY,
    JS_CTOR_FUNCTION,
    JS_CTOR_STRING,
    JS_CTOR_NUMBER,
    JS_CTOR_BOOLEAN,
    JS_CTOR_SYMBOL,
    JS_CTOR_ERROR,
    JS_CTOR_TYPE_ERROR,
    JS_CTOR_RANGE_ERROR,
    JS_CTOR_REFERENCE_ERROR,
    JS_CTOR_SYNTAX_ERROR,
    JS_CTOR_URI_ERROR,
    JS_CTOR_EVAL_ERROR,
    JS_CTOR_REGEXP,
    JS_CTOR_DATE,
    JS_CTOR_PROMISE,
    JS_CTOR_MAP,
    JS_CTOR_SET,
    JS_CTOR_WEAKMAP,
    JS_CTOR_WEAKSET,
    JS_CTOR_ARRAY_BUFFER,
    JS_CTOR_DATAVIEW,
    JS_CTOR_INT8ARRAY,
    JS_CTOR_UINT8ARRAY,
    JS_CTOR_UINT8CLAMPEDARRAY,
    JS_CTOR_INT16ARRAY,
    JS_CTOR_UINT16ARRAY,
    JS_CTOR_INT32ARRAY,
    JS_CTOR_UINT32ARRAY,
    JS_CTOR_FLOAT32ARRAY,
    JS_CTOR_FLOAT64ARRAY,
    JS_CTOR_BIGINT64ARRAY,
    JS_CTOR_BIGUINT64ARRAY,
    JS_CTOR_AGGREGATE_ERROR,
    JS_CTOR_PROXY,
    JS_CTOR_EVENT,
    JS_CTOR_CUSTOM_EVENT,
    JS_CTOR_EVENT_TARGET,
    JS_CTOR_UI_EVENT,
    JS_CTOR_FOCUS_EVENT,
    JS_CTOR_MOUSE_EVENT,
    JS_CTOR_WHEEL_EVENT,
    JS_CTOR_KEYBOARD_EVENT,
    JS_CTOR_COMPOSITION_EVENT,
    JS_CTOR_INPUT_EVENT,
    JS_CTOR_POINTER_EVENT,
    JS_CTOR_MAX
};

static Item js_constructor_cache[JS_CTOR_MAX];
static bool js_ctor_cache_init = false;

// Forward declaration: snapshot mechanism preserves ctor identity across batch resets.
extern "C" bool js_proto_snapshot_is_valid();

void js_ctor_cache_reset() {
    // If snapshot is valid, the harness preamble has already cached references to
    // the constructor Items in its module-vars. Zeroing the cache would force
    // re-creation of NEW JsCtor objects on next access, breaking identity with
    // the harness-cached references. Skip the reset; snapshot/restore handles state.
    if (js_proto_snapshot_is_valid()) return;
    memset(js_constructor_cache, 0, sizeof(js_constructor_cache));
    js_ctor_cache_init = false;
}

// Dummy func_ptr for constructors (makes typeof return "function")
static Item js_ctor_placeholder() { return ItemNull; }

// v49: Constructor that requires 'new' — throws TypeError when called as a function.
// Used for Map, Set, WeakMap, WeakSet, Promise, ArrayBuffer, DataView, etc.
extern "C" Item js_get_new_target();
static Item js_ctor_requires_new() {
    // Check if there's a pending new.target (set by Reflect.construct or 'new')
    Item nt = js_get_new_target();
    if (get_type_id(nt) == LMD_TYPE_FUNC) {
        // Called via Reflect.construct — allow it (return placeholder, actual
        // construction is handled by name-based dispatch in js_new_from_class_object)
        return ItemNull;
    }
    // Called without 'new' — throw TypeError
    Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
    Item msg = (Item){.item = s2it(heap_create_name("Constructor requires 'new'", 26))};
    extern void js_throw_value(Item value);
    extern Item js_new_error_with_name(Item type_name, Item message);
    js_throw_value(js_new_error_with_name(tn, msg));
    return ItemNull;
}

// v18: Real constructor functions for type coercion calls (Boolean(x), Number(x), String(x), Object(x))
static Item js_ctor_object_fn(Item arg) { return js_to_object(arg); }
// v50: Array() called as function — same as new Array() per ES spec
extern "C" Item js_array_new(int length);
extern "C" Item js_array_new_from_item(Item arg);
static Item js_ctor_array_fn(Item arg) {
    // When called with 0 args, arg is padded to undefined
    if (arg.item == ITEM_JS_UNDEFINED) {
        return js_array_new(0);
    }
    return js_array_new_from_item(arg);
}
static Item js_ctor_boolean_fn(Item arg) { return js_to_boolean(arg); }
static Item js_ctor_number_fn(Item arg) { return js_to_number(arg); }
static Item js_ctor_string_fn(Item arg) {
    // String(Symbol()) is allowed — explicit conversion (ES spec 19.1.1)
    if (get_type_id(arg) == LMD_TYPE_INT && it2i(arg) <= -(int64_t)JS_SYMBOL_BASE) {
        return js_symbol_to_string(arg);
    }
    return js_to_string(arg);
}

// RegExp(pattern, flags) without 'new' should behave like new RegExp(pattern, flags)
extern "C" Item js_regexp_construct(Item pattern_item, Item flags_item);
static Item js_ctor_regexp_fn(Item pattern, Item flags) {
    // ES spec 21.2.3.1: when called without 'new', if IsRegExp(pattern) is true,
    // flags is undefined, AND SameValue(NewTarget, pattern.constructor) is true,
    // then return pattern unchanged. Here NewTarget is the RegExp constructor itself.
    if (get_type_id(pattern) == LMD_TYPE_MAP) {
        TypeId fid = get_type_id(flags);
        if (fid == LMD_TYPE_NULL || fid == LMD_TYPE_UNDEFINED) {
            bool has_rd = false;
            js_map_get_fast_ext(pattern.map, "__rd", 4, &has_rd);
            if (has_rd) {
                // IsRegExp: check pattern[@@match] — if defined, use ToBoolean of it.
                bool ok = true;
                Item sym_match_key = (Item){.item = s2it(heap_create_name("__sym_7", 7))};
                Item sym_match = js_property_get(pattern, sym_match_key);
                if (sym_match.item != ItemNull.item && get_type_id(sym_match) != LMD_TYPE_UNDEFINED) {
                    if (!js_is_truthy(sym_match)) ok = false;
                }
                // Check pattern.constructor === RegExp
                if (ok) {
                    Item ctor_key = (Item){.item = s2it(heap_create_name("constructor", 11))};
                    Item pat_ctor = js_property_get(pattern, ctor_key);
                    Item regexp_ctor = js_get_constructor((Item){.item = s2it(heap_create_name("RegExp", 6))});
                    if (pat_ctor.item != regexp_ctor.item) ok = false;
                }
                if (ok) return pattern;
            }
        }
    }
    return js_regexp_construct(pattern, flags);
}

// Date() without 'new' should return a date string (not a Date object)
extern "C" Item js_date_now_string();
static Item js_ctor_date_fn(Item arg) {
    (void)arg;
    return js_date_now_string();
}

// Error(msg) without 'new' should behave like new Error(msg)
static Item js_ctor_error_fn(Item msg) {
    Item tn = (Item){.item = s2it(heap_create_name("Error", 5))};
    return js_new_error_with_name(tn, msg);
}
static Item js_ctor_type_error_fn(Item msg) {
    Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
    return js_new_error_with_name(tn, msg);
}
static Item js_ctor_range_error_fn(Item msg) {
    Item tn = (Item){.item = s2it(heap_create_name("RangeError", 10))};
    return js_new_error_with_name(tn, msg);
}
static Item js_ctor_reference_error_fn(Item msg) {
    Item tn = (Item){.item = s2it(heap_create_name("ReferenceError", 14))};
    return js_new_error_with_name(tn, msg);
}
static Item js_ctor_syntax_error_fn(Item msg) {
    Item tn = (Item){.item = s2it(heap_create_name("SyntaxError", 11))};
    return js_new_error_with_name(tn, msg);
}
static Item js_ctor_uri_error_fn(Item msg) {
    Item tn = (Item){.item = s2it(heap_create_name("URIError", 8))};
    return js_new_error_with_name(tn, msg);
}
static Item js_ctor_eval_error_fn(Item msg) {
    Item tn = (Item){.item = s2it(heap_create_name("EvalError", 9))};
    return js_new_error_with_name(tn, msg);
}

// Event(type, init) / CustomEvent(type, init) -- called without 'new' too.
// Honours EventInitDict {bubbles, cancelable, composed [, detail]} per spec.
static Item js_ctor_event_fn(Item type_arg, Item init_arg) {
    const char* type = fn_to_cstr(type_arg);
    bool bub = false, can = false, comp = false;
    if (get_type_id(init_arg) == LMD_TYPE_MAP) {
        Item bk = (Item){.item = s2it(heap_create_name("bubbles"))};
        Item ck = (Item){.item = s2it(heap_create_name("cancelable"))};
        Item ok = (Item){.item = s2it(heap_create_name("composed"))};
        Item bv = js_property_get(init_arg, bk);
        Item cv = js_property_get(init_arg, ck);
        Item ov = js_property_get(init_arg, ok);
        if (bv.item != 0 && get_type_id(bv) != LMD_TYPE_UNDEFINED) bub = js_is_truthy(bv);
        if (cv.item != 0 && get_type_id(cv) != LMD_TYPE_UNDEFINED) can = js_is_truthy(cv);
        if (ov.item != 0 && get_type_id(ov) != LMD_TYPE_UNDEFINED) comp = js_is_truthy(ov);
    }
    return js_create_event_init(type ? type : "", bub, can, comp);
}
static Item js_ctor_custom_event_fn(Item type_arg, Item init_arg) {
    const char* type = fn_to_cstr(type_arg);
    bool bub = false, can = false, comp = false;
    Item detail = ItemNull;
    if (get_type_id(init_arg) == LMD_TYPE_MAP) {
        Item bk = (Item){.item = s2it(heap_create_name("bubbles"))};
        Item ck = (Item){.item = s2it(heap_create_name("cancelable"))};
        Item ok = (Item){.item = s2it(heap_create_name("composed"))};
        Item dk = (Item){.item = s2it(heap_create_name("detail"))};
        Item bv = js_property_get(init_arg, bk);
        Item cv = js_property_get(init_arg, ck);
        Item ov = js_property_get(init_arg, ok);
        Item dv = js_property_get(init_arg, dk);
        if (bv.item != 0 && get_type_id(bv) != LMD_TYPE_UNDEFINED) bub = js_is_truthy(bv);
        if (cv.item != 0 && get_type_id(cv) != LMD_TYPE_UNDEFINED) can = js_is_truthy(cv);
        if (ov.item != 0 && get_type_id(ov) != LMD_TYPE_UNDEFINED) comp = js_is_truthy(ov);
        if (dv.item != 0) detail = dv;
    }
    return js_create_custom_event_init(type ? type : "", bub, can, comp, detail);
}

// EventTarget() — fresh JS object with addEventListener / removeEventListener /
// dispatchEvent methods. Per spec, callable with `new` only; called as a
// function still returns a fresh target (matches V8 / Firefox behaviour for
// historical EventTarget extension semantics).
static Item js_ctor_event_target_fn() {
    extern Item js_create_event_target(void);
    return js_create_event_target();
}

// Forward declaration of JsFunction struct (matches js_runtime.cpp definition)
struct JsCtor {
    TypeId type_id;
    void* func_ptr;
    int param_count;
    Item* env;
    int env_size;
    Item prototype;
    Item bound_this;
    Item* bound_args;
    int bound_argc;
    String* name;
    int builtin_id;
    Item properties_map; // v18: must match JsFunction layout
    uint8_t flags;       // must match JsFunction layout (generator, arrow flags)
    int16_t formal_length; // must match JsFunction layout
    Item* module_vars;   // must match JsFunction layout
    String* source_text; // must match JsFunction layout (v29)
};

// Reset constructor prototype objects between batch tests.
//
// Strategy: snapshot+restore (preserves Map* identity across batch tests).
//
// The harness preamble caches references like `var TypedArray = Object.getPrototypeOf(Int8Array)`.
// If we destroy these prototypes between tests and lazily recreate them, the harness's
// cached references diverge from fresh `Int8Array.prototype` lookups → identity asserts fail
// downstream (~1400 typed-array test failures).
//
// Instead, on the first reset (post-preamble), we take a deep snapshot of each ctor's
// prototype Map contents (raw bytes of data buffer + type/data/cap pointers).  On subsequent
// resets we restore that snapshot in-place, preserving the Map* address.  Tests that mutate
// built-in prototypes are isolated from each other.
static void js_typed_array_base_reset(); // forward declaration

// %TypedArray% intrinsic: shared base constructor for all TypedArray types.
// (Forward declarations moved up so the snapshot code below can reference them.)
static Item js_typed_array_base = {0};
static Item js_typed_array_base_proto = {0};
#define JS_TYPED_ARRAY_TYPE_COUNT 11
static Item js_typed_array_per_type_proto[JS_TYPED_ARRAY_TYPE_COUNT] = {{0}};

// Map snapshot: captures all mutable Map fields plus a copy of its packed data buffer.
// On restore, the Map's address is preserved; only its contents are reset.
struct MapSnapshot {
    Map*     m;          // identity (NULL = no snapshot)
    void*    type;       // TypeMap* at preamble
    void*    data;       // data buffer pointer at preamble (still pool-allocated)
    int      data_cap;   // data buffer capacity at preamble
    uint8_t  flags;      // map_kind etc.
    int      byte_size;  // TypeMap->byte_size at preamble
    void*    bytes;      // copy of *data (size = byte_size); NULL if byte_size==0
};

struct CtorSnapshot {
    JsCtor* ctor;
    Item    prototype;        // Item value (preserved)
    Item    properties_map;   // Item value (preserved)
    MapSnapshot proto_map;    // contents snapshot of prototype Map (if it is a Map)
    MapSnapshot props_map;    // contents snapshot of properties_map Map (if it is a Map)
    bool    valid;
};

static CtorSnapshot js_ctor_snapshots[JS_CTOR_MAX];
static MapSnapshot  js_typed_array_base_proto_snap;
static Item         js_typed_array_base_snap = {0};
static Item         js_typed_array_base_proto_item_snap = {0};
static Item         js_typed_array_per_type_proto_snap[JS_TYPED_ARRAY_TYPE_COUNT];
static MapSnapshot  js_typed_array_per_type_proto_map_snap[JS_TYPED_ARRAY_TYPE_COUNT];
static bool         js_proto_snapshot_valid = false;

static void js_proto_snapshot_map(MapSnapshot* snap, Map* m) {
    if (!m) { snap->m = NULL; return; }
    TypeMap* tm = (TypeMap*)m->type;
    int byte_size = tm ? (int)tm->byte_size : 0;
    snap->m = m;
    snap->type = m->type;
    snap->data = m->data;
    snap->data_cap = m->data_cap;
    snap->flags = m->flags;
    snap->byte_size = byte_size;
    snap->bytes = NULL;
    if (byte_size > 0 && m->data) {
        snap->bytes = mem_alloc(byte_size, MEM_CAT_JS_RUNTIME);
        memcpy(snap->bytes, m->data, byte_size);
    }
}

static void js_proto_restore_map(const MapSnapshot* snap) {
    if (!snap->m) return;
    Map* m = snap->m;
    m->type = snap->type;
    m->data = snap->data;
    m->data_cap = snap->data_cap;
    m->flags = snap->flags;
    if (snap->byte_size > 0 && snap->data && snap->bytes) {
        memcpy(snap->data, snap->bytes, snap->byte_size);
    }
}

static void js_proto_snapshot_take_locked() {
    js_proto_snapshot_valid = true;
    for (int i = 0; i < JS_CTOR_MAX; i++) {
        CtorSnapshot* s = &js_ctor_snapshots[i];
        s->valid = false;
        s->proto_map.m = NULL;
        s->props_map.m = NULL;
        Item ci = js_constructor_cache[i];
        if (ci.item == 0 || ci.item == ItemNull.item) continue;
        JsCtor* ctor = (JsCtor*)ci.function;
        if (!ctor) continue;
        s->ctor = ctor;
        s->prototype = ctor->prototype;
        s->properties_map = ctor->properties_map;
        s->valid = true;
        if (ctor->prototype.item != 0 && get_type_id(ctor->prototype) == LMD_TYPE_MAP) {
            js_proto_snapshot_map(&s->proto_map, ctor->prototype.map);
        }
        if (ctor->properties_map.item != 0 && get_type_id(ctor->properties_map) == LMD_TYPE_MAP) {
            js_proto_snapshot_map(&s->props_map, ctor->properties_map.map);
        }
    }
    // %TypedArray% intrinsic + its prototype + per-type prototypes
    js_typed_array_base_snap = js_typed_array_base;
    js_typed_array_base_proto_item_snap = js_typed_array_base_proto;
    js_typed_array_base_proto_snap.m = NULL;
    if (js_typed_array_base_proto.item != 0 && get_type_id(js_typed_array_base_proto) == LMD_TYPE_MAP) {
        js_proto_snapshot_map(&js_typed_array_base_proto_snap, js_typed_array_base_proto.map);
    }
    for (int i = 0; i < JS_TYPED_ARRAY_TYPE_COUNT; i++) {
        Item p = js_typed_array_per_type_proto[i];
        js_typed_array_per_type_proto_snap[i] = p;
        js_typed_array_per_type_proto_map_snap[i].m = NULL;
        if (p.item != 0 && get_type_id(p) == LMD_TYPE_MAP) {
            js_proto_snapshot_map(&js_typed_array_per_type_proto_map_snap[i], p.map);
        }
    }
}

static void js_proto_snapshot_restore_locked() {
    for (int i = 0; i < JS_CTOR_MAX; i++) {
        CtorSnapshot* s = &js_ctor_snapshots[i];
        if (!s->valid) continue;
        JsCtor* ctor = s->ctor;
        ctor->prototype = s->prototype;
        ctor->properties_map = s->properties_map;
        if (s->proto_map.m) js_proto_restore_map(&s->proto_map);
        if (s->props_map.m) js_proto_restore_map(&s->props_map);
    }
    js_typed_array_base = js_typed_array_base_snap;
    js_typed_array_base_proto = js_typed_array_base_proto_item_snap;
    if (js_typed_array_base_proto_snap.m) js_proto_restore_map(&js_typed_array_base_proto_snap);
    for (int i = 0; i < JS_TYPED_ARRAY_TYPE_COUNT; i++) {
        js_typed_array_per_type_proto[i] = js_typed_array_per_type_proto_snap[i];
        if (js_typed_array_per_type_proto_map_snap[i].m)
            js_proto_restore_map(&js_typed_array_per_type_proto_map_snap[i]);
    }
}

extern "C" void js_reset_constructor_prototypes() {
    if (!js_ctor_cache_init) return;
    if (!js_proto_snapshot_valid) {
        // First call after preamble: capture snapshot. State is already correct.
        js_proto_snapshot_take_locked();
        // Still clear globalThis so it's regenerated against the snapshotted prototypes.
        js_global_this_obj = (Item){0};
        return;
    }
    // Subsequent calls: restore snapshot (preserves Map* identity).
    js_proto_snapshot_restore_locked();
    js_global_this_obj = (Item){0};
}

extern "C" bool js_proto_snapshot_is_valid() {
    return js_proto_snapshot_valid;
}

// Invalidate snapshot — must be called before pool/heap teardown that frees
// the underlying ctor / Map allocations (e.g. crash recovery in batch mode).
// After this, the next js_reset_constructor_prototypes() will take a fresh
// snapshot rather than restore from stale pointers.
extern "C" void js_proto_snapshot_invalidate() {
    js_proto_snapshot_valid = false;
    for (int i = 0; i < JS_CTOR_MAX; i++) {
        js_ctor_snapshots[i].valid = false;
        js_ctor_snapshots[i].proto_map.m = NULL;
        js_ctor_snapshots[i].props_map.m = NULL;
    }
    js_typed_array_base_proto_snap.m = NULL;
    for (int i = 0; i < JS_TYPED_ARRAY_TYPE_COUNT; i++) {
        js_typed_array_per_type_proto_map_snap[i].m = NULL;
    }
}

// Get the per-type prototype for a given typed array element type.
// Creates it lazily if needed.
extern "C" Item js_get_typed_array_per_type_proto(int element_type);


extern "C" bool js_is_typed_array_ctor_name(const char* name, int len) {
    return (len == 9  && strncmp(name, "Int8Array", 9) == 0) ||
           (len == 10 && strncmp(name, "Uint8Array", 10) == 0) ||
           (len == 17 && strncmp(name, "Uint8ClampedArray", 17) == 0) ||
           (len == 10 && strncmp(name, "Int16Array", 10) == 0) ||
           (len == 11 && strncmp(name, "Uint16Array", 11) == 0) ||
           (len == 10 && strncmp(name, "Int32Array", 10) == 0) ||
           (len == 11 && strncmp(name, "Uint32Array", 11) == 0) ||
           (len == 12 && strncmp(name, "Float32Array", 12) == 0) ||
           (len == 12 && strncmp(name, "Float64Array", 12) == 0) ||
           (len == 13 && strncmp(name, "BigInt64Array", 13) == 0) ||
           (len == 14 && strncmp(name, "BigUint64Array", 14) == 0);
}

extern "C" Item js_get_typed_array_base_proto(); // forward declaration

extern "C" Item js_get_typed_array_base() {
    if (js_typed_array_base.item != 0) return js_typed_array_base;
    // Create the %TypedArray% intrinsic function object
    JsFunctionLayout* fn = (JsFunctionLayout*)pool_calloc(js_input->pool, sizeof(JsFunctionLayout));
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = (void*)js_ctor_placeholder;
    fn->param_count = 0;
    fn->formal_length = -1;
    fn->builtin_id = -2;
    fn->name = heap_create_name("TypedArray", 10);
    js_typed_array_base = (Item){.function = (Function*)fn};
    heap_register_gc_root(&js_typed_array_base.item);
    // Eagerly initialize %TypedArray%.prototype so it's available before any
    // concrete TypedArray prototype chain is set up (e.g., Object.getPrototypeOf(Int8Array).prototype)
    js_get_typed_array_base_proto();
    return js_typed_array_base;
}

// Helper: create a named function stub (bypasses func_ptr cache).
// For prototype method stubs that just need .name and .length properties.
static Item js_create_func_stub(const char* name, int name_len, int param_count) {
    JsFunctionLayout* fn = (JsFunctionLayout*)pool_calloc(js_input->pool, sizeof(JsFunctionLayout));
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = NULL;
    fn->param_count = param_count;
    fn->formal_length = -1;
    fn->builtin_id = 0;
    fn->name = heap_create_name(name, name_len);
    return (Item){.function = (Function*)fn};
}

// Defined in js_runtime.cpp — populates %TypedArray%.prototype with proper Array builtins
extern "C" void js_populate_typed_array_base_proto(Item proto, Item base_ctor);

extern "C" Item js_get_typed_array_base_proto() {
    if (js_typed_array_base_proto.item != 0) return js_typed_array_base_proto;
    js_typed_array_base_proto = js_new_object();
    heap_register_gc_root(&js_typed_array_base_proto.item);
    // Set __class_name__ for type identification
    Item cnk = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
    Item cnv = (Item){.item = s2it(heap_create_name("TypedArray", 10))};
    js_property_set(js_typed_array_base_proto, cnk, cnv);
    // Set __is_proto__ marker
    Item ipk = (Item){.item = s2it(heap_create_name("__is_proto__", 12))};
    js_property_set(js_typed_array_base_proto, ipk, (Item){.item = b2it(true)});
    // A3-T3a: dual-write JsClass byte alongside __class_name__.
    js_class_stamp(js_typed_array_base_proto, JS_CLASS_TYPED_ARRAY);
    // Connect %TypedArray%.prototype to %TypedArray%
    Item base = js_get_typed_array_base();
    JsFunctionLayout* base_fn = (JsFunctionLayout*)base.function;
    base_fn->prototype = js_typed_array_base_proto;
    heap_register_gc_root(&base_fn->prototype.item);

    // Populate methods on %TypedArray%.prototype and static methods on %TypedArray%
    js_populate_typed_array_base_proto(js_typed_array_base_proto, base);

    return js_typed_array_base_proto;
}

static void js_typed_array_base_reset() {
    js_typed_array_base = (Item){0};
    js_typed_array_base_proto = (Item){0};
    for (int i = 0; i < JS_TYPED_ARRAY_TYPE_COUNT; i++)
        js_typed_array_per_type_proto[i] = (Item){0};
}

// Get/create per-type prototype for a typed array element type.
// Sets constructor → concrete constructor, BYTES_PER_ELEMENT, __proto__ → %TypedArray%.prototype
extern "C" Item js_get_typed_array_per_type_proto(int element_type) {
    if (element_type < 0 || element_type >= JS_TYPED_ARRAY_TYPE_COUNT) return js_get_typed_array_base_proto();
    if (js_typed_array_per_type_proto[element_type].item != 0) return js_typed_array_per_type_proto[element_type];

    // Determine constructor name and BYTES_PER_ELEMENT
    const char* ctor_name = NULL;
    int ctor_name_len = 0;
    int bytes_per = 0;
    switch (element_type) {
        case 0: ctor_name = "Int8Array";          ctor_name_len = 9;  bytes_per = 1; break;
        case 1: ctor_name = "Uint8Array";         ctor_name_len = 10; bytes_per = 1; break;
        case 2: ctor_name = "Int16Array";         ctor_name_len = 10; bytes_per = 2; break;
        case 3: ctor_name = "Uint16Array";        ctor_name_len = 11; bytes_per = 2; break;
        case 4: ctor_name = "Int32Array";         ctor_name_len = 10; bytes_per = 4; break;
        case 5: ctor_name = "Uint32Array";        ctor_name_len = 11; bytes_per = 4; break;
        case 6: ctor_name = "Float32Array";       ctor_name_len = 12; bytes_per = 4; break;
        case 7: ctor_name = "Float64Array";       ctor_name_len = 12; bytes_per = 8; break;
        case 8: ctor_name = "Uint8ClampedArray";  ctor_name_len = 17; bytes_per = 1; break;
        case 9: ctor_name = "BigInt64Array";      ctor_name_len = 13; bytes_per = 8; break;
        case 10: ctor_name = "BigUint64Array";    ctor_name_len = 14; bytes_per = 8; break;
        default: return js_get_typed_array_base_proto();
    }

    Item base_proto = js_get_typed_array_base_proto();
    Item per_type = js_new_object();
    heap_register_gc_root(&js_typed_array_per_type_proto[element_type].item);
    js_typed_array_per_type_proto[element_type] = per_type;

    // Set __proto__ to %TypedArray%.prototype
    Item proto_key = (Item){.item = s2it(heap_create_name("__proto__", 9))};
    js_property_set(per_type, proto_key, base_proto);

    // Get the concrete constructor (e.g., Int8Array) and set it as .constructor
    Item ctor_name_item = (Item){.item = s2it(heap_create_name(ctor_name, ctor_name_len))};
    extern Item js_get_constructor(Item name_item);
    Item ctor = js_get_constructor(ctor_name_item);
    Item ctor_key = (Item){.item = s2it(heap_create_name("constructor", 11))};
    js_property_set(per_type, ctor_key, ctor);
    js_mark_non_enumerable(per_type, ctor_key);

    // Set BYTES_PER_ELEMENT on the per-type prototype
    Item bpe_key = (Item){.item = s2it(heap_create_name("BYTES_PER_ELEMENT", 17))};
    Item bpe_val = (Item){.item = i2it(bytes_per)};
    js_property_set(per_type, bpe_key, bpe_val);
    js_mark_non_enumerable(per_type, bpe_key);

    // Also set BYTES_PER_ELEMENT on the constructor itself (static property)
    js_func_init_property(ctor, bpe_key, bpe_val);

    // Set the constructor's .prototype to this per-type proto
    JsFunctionLayout* fn = (JsFunctionLayout*)ctor.function;
    fn->prototype = per_type;

    return per_type;
}

// Forward declarations for functions in js_runtime.cpp used by constructor population
extern "C" void js_func_init_property(Item fn_item, Item key, Item value);
extern "C" void js_mark_non_enumerable(Item object, Item name);
extern "C" void js_mark_non_writable(Item object, Item name);
extern "C" Item js_property_get(Item object, Item key);
extern "C" void js_populate_constructor_statics(Item ctor_item, const char* ctor_name, int ctor_len);

// Populate Number constructor with own properties (constants + static methods)
// so they appear via hasOwnProperty, Object.getOwnPropertyDescriptor, etc.
static void js_populate_number_ctor(Item fn_item) {
    // Constants: non-enumerable, non-writable, non-configurable
    struct { const char* name; int len; double value; } constants[] = {
        {"NEGATIVE_INFINITY", 17, -1.0/0.0},
        {"POSITIVE_INFINITY", 17, 1.0/0.0},
        {"NaN", 3, 0.0/0.0},
        {"MAX_VALUE", 9, 1.7976931348623157e+308},
        {"MIN_VALUE", 9, 5e-324},
        {"MAX_SAFE_INTEGER", 16, 9007199254740991.0},
        {"MIN_SAFE_INTEGER", 16, -9007199254740991.0},
        {"EPSILON", 7, 2.220446049250313e-16},
    };
    for (int i = 0; i < 8; i++) {
        Item key = (Item){.item = s2it(heap_create_name(constants[i].name, constants[i].len))};
        js_func_init_property(fn_item, key, make_double(constants[i].value));
        js_mark_non_enumerable(fn_item, key);
        js_mark_non_writable(fn_item, key);
        js_mark_non_configurable(fn_item, key);
    }
    // Static methods: non-enumerable (writable, configurable by default)
    // Fetch via js_property_get which triggers js_lookup_constructor_static
    const char* methods[] = {"isFinite", "isNaN", "isInteger", "isSafeInteger", "parseInt", "parseFloat"};
    int method_lens[] = {8, 5, 9, 13, 8, 10};
    int method_pcs[] = {1, 1, 1, 1, 2, 1};
    for (int i = 0; i < 6; i++) {
        Item key = (Item){.item = s2it(heap_create_name(methods[i], method_lens[i]))};
        Item method;
        // ES spec: Number.parseInt === parseInt, Number.parseFloat === parseFloat
        // Use the same global builtin function objects for identity equality
        if (i >= 4) { // parseInt (i=4) and parseFloat (i=5)
            method = js_get_global_builtin_fn(key, (Item){.item = i2it(method_pcs[i])});
        } else {
            method = js_property_get(fn_item, key);
        }
        if (method.item != ItemNull.item && method.item != make_js_undefined().item) {
            js_func_init_property(fn_item, key, method);
            js_mark_non_enumerable(fn_item, key);
        }
    }
}

// Populate Symbol constructor with well-known symbol properties (deferred — called from js_create_constructor)
static void js_populate_symbol_ctor(Item fn_item);

static Item js_create_constructor(int ctor_id, const char* name, int param_count) {
    if (!js_ctor_cache_init) {
        for (int i = 0; i < JS_CTOR_MAX; i++) js_constructor_cache[i] = ItemNull;
        js_ctor_cache_init = true;
    }
    if (js_constructor_cache[ctor_id].item != ItemNull.item) {
        return js_constructor_cache[ctor_id];
    }
    // Allocate directly via pool — do NOT use js_new_function() because it
    // caches by func_ptr and all constructors share js_ctor_placeholder.
    JsCtor* fn = (JsCtor*)pool_calloc(js_input->pool, sizeof(JsCtor));
    fn->type_id = LMD_TYPE_FUNC;
    // v18: Use real function pointers for type coercion constructors
    if (ctor_id == JS_CTOR_BOOLEAN) fn->func_ptr = (void*)js_ctor_boolean_fn;
    else if (ctor_id == JS_CTOR_NUMBER) fn->func_ptr = (void*)js_ctor_number_fn;
    else if (ctor_id == JS_CTOR_STRING) fn->func_ptr = (void*)js_ctor_string_fn;
    else if (ctor_id == JS_CTOR_OBJECT) fn->func_ptr = (void*)js_ctor_object_fn;
    else if (ctor_id == JS_CTOR_ARRAY) fn->func_ptr = (void*)js_ctor_array_fn;
    else if (ctor_id == JS_CTOR_REGEXP) fn->func_ptr = (void*)js_ctor_regexp_fn;
    else if (ctor_id == JS_CTOR_DATE) fn->func_ptr = (void*)js_ctor_date_fn;
    else if (ctor_id == JS_CTOR_ERROR) fn->func_ptr = (void*)js_ctor_error_fn;
    else if (ctor_id == JS_CTOR_TYPE_ERROR) fn->func_ptr = (void*)js_ctor_type_error_fn;
    else if (ctor_id == JS_CTOR_RANGE_ERROR) fn->func_ptr = (void*)js_ctor_range_error_fn;
    else if (ctor_id == JS_CTOR_REFERENCE_ERROR) fn->func_ptr = (void*)js_ctor_reference_error_fn;
    else if (ctor_id == JS_CTOR_SYNTAX_ERROR) fn->func_ptr = (void*)js_ctor_syntax_error_fn;
    else if (ctor_id == JS_CTOR_URI_ERROR) fn->func_ptr = (void*)js_ctor_uri_error_fn;
    else if (ctor_id == JS_CTOR_EVAL_ERROR) fn->func_ptr = (void*)js_ctor_eval_error_fn;
    else if (ctor_id == JS_CTOR_EVENT) fn->func_ptr = (void*)js_ctor_event_fn;
    else if (ctor_id == JS_CTOR_CUSTOM_EVENT) fn->func_ptr = (void*)js_ctor_custom_event_fn;
    else if (ctor_id == JS_CTOR_EVENT_TARGET) fn->func_ptr = (void*)js_ctor_event_target_fn;
    else if (ctor_id == JS_CTOR_UI_EVENT) fn->func_ptr = (void*)js_ctor_ui_event_fn;
    else if (ctor_id == JS_CTOR_FOCUS_EVENT) fn->func_ptr = (void*)js_ctor_focus_event_fn;
    else if (ctor_id == JS_CTOR_MOUSE_EVENT) fn->func_ptr = (void*)js_ctor_mouse_event_fn;
    else if (ctor_id == JS_CTOR_WHEEL_EVENT) fn->func_ptr = (void*)js_ctor_wheel_event_fn;
    else if (ctor_id == JS_CTOR_KEYBOARD_EVENT) fn->func_ptr = (void*)js_ctor_keyboard_event_fn;
    else if (ctor_id == JS_CTOR_COMPOSITION_EVENT) fn->func_ptr = (void*)js_ctor_composition_event_fn;
    else if (ctor_id == JS_CTOR_INPUT_EVENT) fn->func_ptr = (void*)js_ctor_input_event_fn;
    else if (ctor_id == JS_CTOR_POINTER_EVENT) fn->func_ptr = (void*)js_ctor_pointer_event_fn;
    else if (ctor_id == JS_CTOR_PROMISE || ctor_id == JS_CTOR_MAP || ctor_id == JS_CTOR_SET ||
             ctor_id == JS_CTOR_WEAKMAP || ctor_id == JS_CTOR_WEAKSET ||
             ctor_id == JS_CTOR_ARRAY_BUFFER || ctor_id == JS_CTOR_DATAVIEW ||
             ctor_id == JS_CTOR_PROXY ||
             (ctor_id >= JS_CTOR_INT8ARRAY && ctor_id <= JS_CTOR_BIGUINT64ARRAY))
        fn->func_ptr = (void*)js_ctor_requires_new;
    else fn->func_ptr = (void*)js_ctor_placeholder;
    fn->param_count = param_count;
    fn->formal_length = -1; // -1 = use param_count for .length
    fn->env = NULL;
    fn->env_size = 0;
    fn->prototype = ItemNull;
    // NOTE: bound_this left as 0 (from pool_calloc). Do NOT set to ItemNull
    // because ItemNull.item is non-zero and bound check uses truthy test.
    fn->bound_args = NULL;
    fn->bound_argc = 0;
    fn->name = heap_create_name(name, strlen(name));
    fn->builtin_id = 0;
    Item fn_item = (Item){.function = (Function*)fn};
    js_constructor_cache[ctor_id] = fn_item;
    // Populate constructor-specific own properties
    if (ctor_id == JS_CTOR_NUMBER) js_populate_number_ctor(fn_item);
    if (ctor_id == JS_CTOR_SYMBOL) js_populate_symbol_ctor(fn_item);
    if (ctor_id == JS_CTOR_EVENT || ctor_id == JS_CTOR_CUSTOM_EVENT) {
        // Static phase constants on Event / CustomEvent constructor + prototype.
        struct { const char* n; int v; } ph[] = {
            {"NONE", 0}, {"CAPTURING_PHASE", 1}, {"AT_TARGET", 2}, {"BUBBLING_PHASE", 3}
        };
        Item proto = js_new_object();
        for (int i = 0; i < 4; i++) {
            Item k = (Item){.item = s2it(heap_create_name(ph[i].n, strlen(ph[i].n)))};
            Item v = (Item){.item = i2it(ph[i].v)};
            js_func_init_property(fn_item, k, v);
            js_property_set(proto, k, v);
        }
        // .constructor on prototype points back to the function.
        Item ck = (Item){.item = s2it(heap_create_name("constructor", 11))};
        js_property_set(proto, ck, fn_item);
        ((JsCtor*)fn_item.function)->prototype = proto;
    }
    // Populate static methods as own properties for all constructors
    js_populate_constructor_statics(fn_item, name, strlen(name));
    // TypedArray constructors: set up per-type prototype with constructor + BYTES_PER_ELEMENT
    if (ctor_id >= JS_CTOR_INT8ARRAY && ctor_id <= JS_CTOR_BIGUINT64ARRAY) {
        // Map ctor_id to JsTypedArrayType element_type
        int element_type = -1;
        switch (ctor_id) {
            case JS_CTOR_INT8ARRAY:        element_type = 0; break; // JS_TYPED_INT8
            case JS_CTOR_UINT8ARRAY:       element_type = 1; break; // JS_TYPED_UINT8
            case JS_CTOR_UINT8CLAMPEDARRAY:element_type = 8; break; // JS_TYPED_UINT8_CLAMPED
            case JS_CTOR_INT16ARRAY:       element_type = 2; break; // JS_TYPED_INT16
            case JS_CTOR_UINT16ARRAY:      element_type = 3; break; // JS_TYPED_UINT16
            case JS_CTOR_INT32ARRAY:       element_type = 4; break; // JS_TYPED_INT32
            case JS_CTOR_UINT32ARRAY:      element_type = 5; break; // JS_TYPED_UINT32
            case JS_CTOR_FLOAT32ARRAY:     element_type = 6; break; // JS_TYPED_FLOAT32
            case JS_CTOR_FLOAT64ARRAY:     element_type = 7; break; // JS_TYPED_FLOAT64
            case JS_CTOR_BIGINT64ARRAY:    element_type = 9; break; // JS_TYPED_BIGINT64
            case JS_CTOR_BIGUINT64ARRAY:   element_type = 10; break; // JS_TYPED_BIGUINT64
            default: break;
        }
        if (element_type >= 0) {
            // js_get_typed_array_per_type_proto sets fn->prototype and adds BYTES_PER_ELEMENT
            js_get_typed_array_per_type_proto(element_type);
        }
    }
    // Error.captureStackTrace — V8-specific no-op stub (sets .stack on target)
    if (ctor_id == JS_CTOR_ERROR) {
        extern Item js_new_function(void* func_ptr, int param_count);
        Item cst_fn = js_new_function((void*)js_error_captureStackTrace, 2);
        Item cst_key = (Item){.item = s2it(heap_create_name("captureStackTrace", 17))};
        js_func_init_property(fn_item, cst_key, cst_fn);
    }
    return fn_item;
}

extern "C" Item js_get_constructor(Item name_item) {
    if (get_type_id(name_item) != LMD_TYPE_STRING) return make_js_undefined();
    String* name = it2s(name_item);
    if (!name) return make_js_undefined();

    struct { const char* name; int len; int id; int pc; } ctors[] = {
        {"Object", 6, JS_CTOR_OBJECT, 1},
        {"Array", 5, JS_CTOR_ARRAY, 1},
        {"Function", 8, JS_CTOR_FUNCTION, 1},
        {"String", 6, JS_CTOR_STRING, 1},
        {"Number", 6, JS_CTOR_NUMBER, 1},
        {"Boolean", 7, JS_CTOR_BOOLEAN, 1},
        {"Symbol", 6, JS_CTOR_SYMBOL, 0},
        {"Error", 5, JS_CTOR_ERROR, 1},
        {"TypeError", 9, JS_CTOR_TYPE_ERROR, 1},
        {"RangeError", 10, JS_CTOR_RANGE_ERROR, 1},
        {"ReferenceError", 14, JS_CTOR_REFERENCE_ERROR, 1},
        {"SyntaxError", 11, JS_CTOR_SYNTAX_ERROR, 1},
        {"URIError", 8, JS_CTOR_URI_ERROR, 1},
        {"EvalError", 9, JS_CTOR_EVAL_ERROR, 1},
        {"AggregateError", 14, JS_CTOR_AGGREGATE_ERROR, 2},
        {"RegExp", 6, JS_CTOR_REGEXP, 2},
        {"Date", 4, JS_CTOR_DATE, 7},
        {"Promise", 7, JS_CTOR_PROMISE, 1},
        {"Map", 3, JS_CTOR_MAP, 0},
        {"Set", 3, JS_CTOR_SET, 0},
        {"WeakMap", 7, JS_CTOR_WEAKMAP, 0},
        {"WeakSet", 7, JS_CTOR_WEAKSET, 0},
        {"ArrayBuffer", 11, JS_CTOR_ARRAY_BUFFER, 1},
        {"DataView", 8, JS_CTOR_DATAVIEW, 1},
        {"Int8Array", 9, JS_CTOR_INT8ARRAY, 3},
        {"Uint8Array", 10, JS_CTOR_UINT8ARRAY, 3},
        {"Uint8ClampedArray", 17, JS_CTOR_UINT8CLAMPEDARRAY, 3},
        {"Int16Array", 10, JS_CTOR_INT16ARRAY, 3},
        {"Uint16Array", 11, JS_CTOR_UINT16ARRAY, 3},
        {"Int32Array", 10, JS_CTOR_INT32ARRAY, 3},
        {"Uint32Array", 11, JS_CTOR_UINT32ARRAY, 3},
        {"Float32Array", 12, JS_CTOR_FLOAT32ARRAY, 3},
        {"Float64Array", 12, JS_CTOR_FLOAT64ARRAY, 3},
        {"BigInt64Array", 13, JS_CTOR_BIGINT64ARRAY, 3},
        {"BigUint64Array", 14, JS_CTOR_BIGUINT64ARRAY, 3},
        {"Proxy", 5, JS_CTOR_PROXY, 2},
        {"Event", 5, JS_CTOR_EVENT, 2},
        {"CustomEvent", 11, JS_CTOR_CUSTOM_EVENT, 2},
        {"EventTarget", 11, JS_CTOR_EVENT_TARGET, 0},
        {"UIEvent", 7, JS_CTOR_UI_EVENT, 2},
        {"FocusEvent", 10, JS_CTOR_FOCUS_EVENT, 2},
        {"MouseEvent", 10, JS_CTOR_MOUSE_EVENT, 2},
        {"WheelEvent", 10, JS_CTOR_WHEEL_EVENT, 2},
        {"KeyboardEvent", 13, JS_CTOR_KEYBOARD_EVENT, 2},
        {"CompositionEvent", 16, JS_CTOR_COMPOSITION_EVENT, 2},
        {"InputEvent", 10, JS_CTOR_INPUT_EVENT, 2},
        {"PointerEvent", 12, JS_CTOR_POINTER_EVENT, 2},
        {NULL, 0, 0, 0}
    };

    for (int i = 0; ctors[i].name; i++) {
        if ((int)name->len == ctors[i].len && strncmp(name->chars, ctors[i].name, name->len) == 0) {
            return js_create_constructor(ctors[i].id, ctors[i].name, ctors[i].pc);
        }
    }
    return make_js_undefined();
}

// =============================================================================
// v12: Symbol API
// =============================================================================

#include "../../lib/hashmap.h"

// symbol registry entry for Symbol.for() / Symbol.keyFor()
struct JsSymbolEntry {
    char key[128];
    uint64_t symbol_id;
};

static uint64_t js_symbol_next_id = 100;  // IDs 1-99 reserved for well-known symbols
static HashMap* js_symbol_registry = nullptr;  // string key -> JsSymbolEntry

// symbol description registry: maps symbol_id -> description string
struct JsSymbolDesc {
    uint64_t symbol_id;
    char desc[128];
    int desc_len;    // -1 means no description (Symbol() with no arg)
};

static HashMap* js_symbol_desc_registry = nullptr;

static int js_symbol_desc_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    return ((const JsSymbolDesc*)a)->symbol_id != ((const JsSymbolDesc*)b)->symbol_id;
}

static uint64_t js_symbol_desc_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const JsSymbolDesc* e = (const JsSymbolDesc*)item;
    return hashmap_sip(&e->symbol_id, sizeof(uint64_t), seed0, seed1);
}

static void js_symbol_desc_init() {
    if (!js_symbol_desc_registry) {
        js_symbol_desc_registry = hashmap_new(sizeof(JsSymbolDesc), 16, 0, 0,
            js_symbol_desc_hash, js_symbol_desc_compare, NULL, NULL);
    }
}

// well-known symbol IDs (pre-allocated)
#define JS_SYMBOL_ID_ITERATOR       1
#define JS_SYMBOL_ID_TO_PRIMITIVE   2
#define JS_SYMBOL_ID_HAS_INSTANCE   3
#define JS_SYMBOL_ID_TO_STRING_TAG  4
#define JS_SYMBOL_ID_ASYNC_ITERATOR 5
#define JS_SYMBOL_ID_SPECIES        6
#define JS_SYMBOL_ID_MATCH          7
#define JS_SYMBOL_ID_REPLACE        8
#define JS_SYMBOL_ID_SEARCH         9
#define JS_SYMBOL_ID_SPLIT          10
#define JS_SYMBOL_ID_UNSCOPABLES    11
#define JS_SYMBOL_ID_IS_CONCAT_SPREADABLE 12
#define JS_SYMBOL_ID_MATCH_ALL     13

static int js_symbol_entry_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    return strcmp(((const JsSymbolEntry*)a)->key, ((const JsSymbolEntry*)b)->key);
}

static uint64_t js_symbol_entry_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const JsSymbolEntry* e = (const JsSymbolEntry*)item;
    return hashmap_sip(e->key, strlen(e->key), seed0, seed1);
}

static void js_symbol_init_registry() {
    if (!js_symbol_registry) {
        js_symbol_registry = hashmap_new(sizeof(JsSymbolEntry), 16, 0, 0,
            js_symbol_entry_hash, js_symbol_entry_compare, NULL, NULL);
    }
}

// create Item encoding for a symbol: use LMD_TYPE_INT with a high-bit marker
// symbol items are encoded as negative ints that won't collide with normal ints
static Item js_make_symbol_item(uint64_t id) {
    // encode as an int with a special range: -(id + JS_SYMBOL_BASE)
    return (Item){.item = i2it(-(int64_t)(id + JS_SYMBOL_BASE))};
}

static bool js_is_symbol_item(Item item) {
    if (get_type_id(item) != LMD_TYPE_INT) return false;
    int64_t v = it2i(item);
    return v <= -(int64_t)JS_SYMBOL_BASE;
}

static uint64_t js_symbol_item_id(Item item) {
    return (uint64_t)(-(it2i(item) + (int64_t)JS_SYMBOL_BASE));
}

// Populate Symbol constructor with well-known symbol properties
// so they appear via hasOwnProperty, Object.getOwnPropertyDescriptor, etc.
// Per ES §19.4.2: each is {writable: false, enumerable: false, configurable: false}
static void js_populate_symbol_ctor(Item fn_item) {
    struct { const char* name; int len; int sym_id; } well_known[] = {
        {"asyncIterator", 13, JS_SYMBOL_ID_ASYNC_ITERATOR},
        {"hasInstance", 11, JS_SYMBOL_ID_HAS_INSTANCE},
        {"isConcatSpreadable", 18, JS_SYMBOL_ID_IS_CONCAT_SPREADABLE},
        {"iterator", 8, JS_SYMBOL_ID_ITERATOR},
        {"match", 5, JS_SYMBOL_ID_MATCH},
        {"matchAll", 8, JS_SYMBOL_ID_MATCH_ALL},
        {"replace", 7, JS_SYMBOL_ID_REPLACE},
        {"search", 6, JS_SYMBOL_ID_SEARCH},
        {"species", 7, JS_SYMBOL_ID_SPECIES},
        {"split", 5, JS_SYMBOL_ID_SPLIT},
        {"toPrimitive", 11, JS_SYMBOL_ID_TO_PRIMITIVE},
        {"toStringTag", 11, JS_SYMBOL_ID_TO_STRING_TAG},
        {"unscopables", 11, JS_SYMBOL_ID_UNSCOPABLES},
    };
    for (int i = 0; i < 13; i++) {
        Item key = (Item){.item = s2it(heap_create_name(well_known[i].name, well_known[i].len))};
        Item value = js_make_symbol_item(well_known[i].sym_id);
        js_func_init_property(fn_item, key, value);
        js_mark_non_enumerable(fn_item, key);
        js_mark_non_writable(fn_item, key);
        js_mark_non_configurable(fn_item, key);
    }
}

extern "C" Item js_symbol_create(Item description) {
    uint64_t id = js_symbol_next_id++;
    Item sym = js_make_symbol_item(id);

    // store description for Symbol.prototype.description
    js_symbol_desc_init();
    JsSymbolDesc entry;
    entry.symbol_id = id;
    if (description.item == ITEM_NULL || description.item == ITEM_JS_UNDEFINED) {
        entry.desc[0] = '\0';
        entry.desc_len = -1;  // no description
    } else {
        String* s = it2s(js_to_string(description));
        if (s) {
            int len = s->len < 127 ? (int)s->len : 127;
            memcpy(entry.desc, s->chars, len);
            entry.desc[len] = '\0';
            entry.desc_len = len;
        } else {
            entry.desc[0] = '\0';
            entry.desc_len = -1;
        }
    }
    hashmap_set(js_symbol_desc_registry, &entry);

    return sym;
}

extern "C" Item js_symbol_for(Item key) {
    js_symbol_init_registry();
    String* s = it2s(js_to_string(key));
    if (!s) return js_symbol_create(key);

    JsSymbolEntry lookup;
    int klen = s->len < 127 ? (int)s->len : 127;
    memcpy(lookup.key, s->chars, klen);
    lookup.key[klen] = '\0';

    JsSymbolEntry* found = (JsSymbolEntry*)hashmap_get(js_symbol_registry, &lookup);
    if (found) return js_make_symbol_item(found->symbol_id);

    // create new entry
    lookup.symbol_id = js_symbol_next_id++;
    hashmap_set(js_symbol_registry, &lookup);
    return js_make_symbol_item(lookup.symbol_id);
}

extern "C" Item js_symbol_key_for(Item sym) {
    if (!js_is_symbol_item(sym))
        return js_throw_type_error("Symbol.keyFor requires a Symbol argument");
    js_symbol_init_registry();
    uint64_t id = js_symbol_item_id(sym);

    // linear scan — symbol registry is small
    size_t iter = 0;
    void* entry;
    while (hashmap_iter(js_symbol_registry, &iter, &entry)) {
        JsSymbolEntry* e = (JsSymbolEntry*)entry;
        if (e->symbol_id == id) {
            return (Item){.item = s2it(heap_create_name(e->key, strlen(e->key)))};
        }
    }
    return make_js_undefined();  // not in global registry → undefined per spec
}

extern "C" Item js_symbol_to_string(Item sym) {
    if (!js_is_symbol_item(sym)) {
        return (Item){.item = s2it(heap_create_name("Symbol()", 8))};
    }
    // check global registry for description
    uint64_t id = js_symbol_item_id(sym);

    // well-known symbols
    if (id == JS_SYMBOL_ID_ITERATOR)       return (Item){.item = s2it(heap_create_name("Symbol(Symbol.iterator)", 23))};
    if (id == JS_SYMBOL_ID_TO_PRIMITIVE)   return (Item){.item = s2it(heap_create_name("Symbol(Symbol.toPrimitive)", 26))};
    if (id == JS_SYMBOL_ID_HAS_INSTANCE)   return (Item){.item = s2it(heap_create_name("Symbol(Symbol.hasInstance)", 26))};
    if (id == JS_SYMBOL_ID_TO_STRING_TAG)  return (Item){.item = s2it(heap_create_name("Symbol(Symbol.toStringTag)", 26))};
    if (id == JS_SYMBOL_ID_ASYNC_ITERATOR) return (Item){.item = s2it(heap_create_name("Symbol(Symbol.asyncIterator)", 28))};
    if (id == JS_SYMBOL_ID_SPECIES)        return (Item){.item = s2it(heap_create_name("Symbol(Symbol.species)", 22))};
    if (id == JS_SYMBOL_ID_MATCH)          return (Item){.item = s2it(heap_create_name("Symbol(Symbol.match)", 20))};
    if (id == JS_SYMBOL_ID_REPLACE)        return (Item){.item = s2it(heap_create_name("Symbol(Symbol.replace)", 22))};
    if (id == JS_SYMBOL_ID_SEARCH)         return (Item){.item = s2it(heap_create_name("Symbol(Symbol.search)", 21))};
    if (id == JS_SYMBOL_ID_SPLIT)          return (Item){.item = s2it(heap_create_name("Symbol(Symbol.split)", 20))};
    if (id == JS_SYMBOL_ID_UNSCOPABLES)    return (Item){.item = s2it(heap_create_name("Symbol(Symbol.unscopables)", 26))};
    if (id == JS_SYMBOL_ID_IS_CONCAT_SPREADABLE) return (Item){.item = s2it(heap_create_name("Symbol(Symbol.isConcatSpreadable)", 32))};

    // check registry
    if (js_symbol_registry) {
        size_t iter = 0;
        void* entry;
        while (hashmap_iter(js_symbol_registry, &iter, &entry)) {
            JsSymbolEntry* e = (JsSymbolEntry*)entry;
            if (e->symbol_id == id) {
                char buf[160];
                snprintf(buf, sizeof(buf), "Symbol(%s)", e->key);
                return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
            }
        }
    }

    // check description registry
    if (js_symbol_desc_registry) {
        JsSymbolDesc lookup;
        lookup.symbol_id = id;
        JsSymbolDesc* found = (JsSymbolDesc*)hashmap_get(js_symbol_desc_registry, &lookup);
        if (found && found->desc_len >= 0) {
            char buf[160];
            snprintf(buf, sizeof(buf), "Symbol(%s)", found->desc);
            return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
        }
    }

    return (Item){.item = s2it(heap_create_name("Symbol()", 8))};
}

// Return the description of a symbol, or undefined if none
extern "C" Item js_symbol_get_description(Item sym) {
    if (!js_is_symbol_item(sym)) return make_js_undefined();
    uint64_t id = js_symbol_item_id(sym);

    // well-known symbols have fixed descriptions
    if (id == JS_SYMBOL_ID_ITERATOR)       return (Item){.item = s2it(heap_create_name("Symbol.iterator", 15))};
    if (id == JS_SYMBOL_ID_TO_PRIMITIVE)   return (Item){.item = s2it(heap_create_name("Symbol.toPrimitive", 18))};
    if (id == JS_SYMBOL_ID_HAS_INSTANCE)   return (Item){.item = s2it(heap_create_name("Symbol.hasInstance", 18))};
    if (id == JS_SYMBOL_ID_TO_STRING_TAG)  return (Item){.item = s2it(heap_create_name("Symbol.toStringTag", 18))};
    if (id == JS_SYMBOL_ID_ASYNC_ITERATOR) return (Item){.item = s2it(heap_create_name("Symbol.asyncIterator", 20))};
    if (id == JS_SYMBOL_ID_SPECIES)        return (Item){.item = s2it(heap_create_name("Symbol.species", 14))};
    if (id == JS_SYMBOL_ID_MATCH)          return (Item){.item = s2it(heap_create_name("Symbol.match", 12))};
    if (id == JS_SYMBOL_ID_REPLACE)        return (Item){.item = s2it(heap_create_name("Symbol.replace", 14))};
    if (id == JS_SYMBOL_ID_SEARCH)         return (Item){.item = s2it(heap_create_name("Symbol.search", 13))};
    if (id == JS_SYMBOL_ID_SPLIT)          return (Item){.item = s2it(heap_create_name("Symbol.split", 12))};
    if (id == JS_SYMBOL_ID_UNSCOPABLES)    return (Item){.item = s2it(heap_create_name("Symbol.unscopables", 18))};
    if (id == JS_SYMBOL_ID_IS_CONCAT_SPREADABLE) return (Item){.item = s2it(heap_create_name("Symbol.isConcatSpreadable", 24))};

    // check Symbol.for() registry
    if (js_symbol_registry) {
        size_t iter = 0;
        void* entry;
        while (hashmap_iter(js_symbol_registry, &iter, &entry)) {
            JsSymbolEntry* e = (JsSymbolEntry*)entry;
            if (e->symbol_id == id) {
                return (Item){.item = s2it(heap_create_name(e->key, strlen(e->key)))};
            }
        }
    }

    // check description registry
    if (js_symbol_desc_registry) {
        JsSymbolDesc lookup;
        lookup.symbol_id = id;
        JsSymbolDesc* found = (JsSymbolDesc*)hashmap_get(js_symbol_desc_registry, &lookup);
        if (found) {
            if (found->desc_len < 0) return make_js_undefined();  // Symbol() with no arg
            return (Item){.item = s2it(heap_create_name(found->desc, found->desc_len))};
        }
    }

    return make_js_undefined();
}

// Return a well-known symbol by its property name on the Symbol constructor.
// e.g. Symbol.iterator → fixed ID=1, Symbol.toPrimitive → fixed ID=2, etc.
// Unlike js_symbol_create(), this always returns the SAME item for a given name.
extern "C" Item js_symbol_builtin_method(int which);

extern "C" Item js_symbol_well_known(Item name) {
    String* s = it2s(name);
    if (s) {
        // Symbol static methods — return builtin functions, not well-known symbols
        if (s->len == 3 && strncmp(s->chars, "for", 3) == 0)
            return js_symbol_builtin_method(0);
        if (s->len == 6 && strncmp(s->chars, "keyFor", 6) == 0)
            return js_symbol_builtin_method(1);
        if (s->len == 8 && strncmp(s->chars, "iterator", 8) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_ITERATOR);
        if (s->len == 11 && strncmp(s->chars, "toPrimitive", 11) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_TO_PRIMITIVE);
        if (s->len == 11 && strncmp(s->chars, "hasInstance", 11) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_HAS_INSTANCE);
        if (s->len == 11 && strncmp(s->chars, "toStringTag", 11) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_TO_STRING_TAG);
        if (s->len == 13 && strncmp(s->chars, "asyncIterator", 13) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_ASYNC_ITERATOR);
        if (s->len == 7 && strncmp(s->chars, "species", 7) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_SPECIES);
        if (s->len == 5 && strncmp(s->chars, "match", 5) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_MATCH);
        if (s->len == 8 && strncmp(s->chars, "matchAll", 8) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_MATCH_ALL);
        if (s->len == 7 && strncmp(s->chars, "replace", 7) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_REPLACE);
        if (s->len == 6 && strncmp(s->chars, "search", 6) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_SEARCH);
        if (s->len == 5 && strncmp(s->chars, "split", 5) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_SPLIT);
        if (s->len == 11 && strncmp(s->chars, "unscopables", 11) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_UNSCOPABLES);
        if (s->len == 18 && strncmp(s->chars, "isConcatSpreadable", 18) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_IS_CONCAT_SPREADABLE);
    }
    // Unknown well-known symbol — create a stable entry via Symbol.for semantics
    return js_symbol_for(name);
}

// =============================================================================
// URL Constructor — wraps lib/url.c
// =============================================================================

extern "C" Item js_url_search_params_new(Item init);

static Item js_url_to_object(Url* url) {
    if (!url || !url->is_valid) {
        if (url) url_destroy(url);
        return ItemNull;
    }
    Item obj = js_new_object();

    // Helper macro to set a string property
    #define URL_SET_PROP(propname, getter) do { \
        const char* _v = getter(url); \
        Item _key = (Item){.item = s2it(heap_create_name(propname))}; \
        Item _val = _v ? (Item){.item = s2it(heap_create_name(_v, strlen(_v)))} : (Item){.item = s2it(heap_create_name("", 0))}; \
        js_property_set(obj, _key, _val); \
    } while(0)

    URL_SET_PROP("href", url_get_href);
    // Compute origin: protocol + "//" + host (hostname + optional port)
    {
        const char* proto = url_get_protocol(url);
        const char* host = url_get_host(url);
        const char* hostname = url_get_hostname(url);
        // For schemes like mailto:, tel: — origin is "null"
        bool has_authority = proto && (strncmp(proto, "http", 4) == 0 ||
                                       strncmp(proto, "ftp", 3) == 0 ||
                                       strncmp(proto, "ws", 2) == 0);
        char origin_buf[512];
        if (has_authority && hostname && hostname[0]) {
            const char* h = (host && host[0]) ? host : hostname;
            snprintf(origin_buf, sizeof(origin_buf), "%s//%s", proto ? proto : "", h);
        } else {
            snprintf(origin_buf, sizeof(origin_buf), "null");
        }
        Item o_key = (Item){.item = s2it(heap_create_name("origin"))};
        Item o_val = (Item){.item = s2it(heap_create_name(origin_buf, strlen(origin_buf)))};
        js_property_set(obj, o_key, o_val);
    }
    URL_SET_PROP("protocol", url_get_protocol);
    URL_SET_PROP("username", url_get_username);
    URL_SET_PROP("password", url_get_password);
    URL_SET_PROP("host", url_get_host);
    URL_SET_PROP("hostname", url_get_hostname);
    URL_SET_PROP("port", url_get_port);
    URL_SET_PROP("pathname", url_get_pathname);
    URL_SET_PROP("search", url_get_search);
    URL_SET_PROP("hash", url_get_hash);

    #undef URL_SET_PROP

    // searchParams — full URLSearchParams object
    {
        const char* search = url_get_search(url);
        Item search_str;
        if (search && search[0]) {
            search_str = (Item){.item = s2it(heap_create_name(search, strlen(search)))};
        } else {
            search_str = (Item){.item = s2it(heap_create_name("", 0))};
        }
        js_property_set(obj, (Item){.item = s2it(heap_create_name("searchParams"))},
                        js_url_search_params_new(search_str));
    }

    // T5b: legacy `__class_name__` string write retired; typed JsClass byte
    // is the URL class identity.
    js_class_stamp(obj, JS_CLASS_URL);  // A3-T3b

    url_destroy(url);
    return obj;
}

extern "C" Item js_url_construct(Item input) {
    TypeId tid = get_type_id(input);
    if (tid != LMD_TYPE_STRING) return ItemNull;
    String* s = it2s(input);
    if (!s || s->len == 0) return ItemNull;

    Url* url = url_parse(s->chars);
    return js_url_to_object(url);
}

extern "C" Item js_url_construct_with_base(Item input, Item base) {
    TypeId tid_base = get_type_id(base);
    if (tid_base != LMD_TYPE_STRING) {
        return js_url_construct(input);
    }
    String* base_str = it2s(base);
    if (!base_str || base_str->len == 0) {
        return js_url_construct(input);
    }

    TypeId tid = get_type_id(input);
    if (tid != LMD_TYPE_STRING) return ItemNull;
    String* s = it2s(input);
    if (!s) return ItemNull;

    Url* base_url = url_parse(base_str->chars);
    if (!base_url || !base_url->is_valid) {
        if (base_url) url_destroy(base_url);
        return ItemNull;
    }
    Url* url = url_parse_with_base(s->chars, base_url);
    url_destroy(base_url);
    return js_url_to_object(url);
}

extern "C" Item js_url_parse(Item input, Item base) {
    TypeId tid = get_type_id(input);
    if (tid != LMD_TYPE_STRING) return ItemNull;

    TypeId tid_base = get_type_id(base);
    if (tid_base == LMD_TYPE_STRING) {
        return js_url_construct_with_base(input, base);
    }
    return js_url_construct(input);
}

extern "C" Item js_url_can_parse(Item input) {
    TypeId tid = get_type_id(input);
    if (tid != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* s = it2s(input);
    if (!s || s->len == 0) return (Item){.item = b2it(false)};

    Url* url = url_parse(s->chars);
    if (!url) return (Item){.item = b2it(false)};
    bool valid = url->is_valid;
    url_destroy(url);
    return valid ? (Item){.item = b2it(true)} : (Item){.item = b2it(false)};
}

// ReadableStream stub — Web Streams API minimal implementation.
// The test checks: typeof readable.getReader === "function"
static Item js_readable_stream_get_reader_stub(void) {
    return js_new_object();
}

extern "C" Item js_readable_stream_new(void) {
    Item obj = js_new_object();
    Item class_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    Item class_val = (Item){.item = s2it(heap_create_name("ReadableStream"))};
    js_property_set(obj, class_key, class_val);
    js_class_stamp(obj, JS_CLASS_READABLE_STREAM);  // A3-T3b
    Item get_reader_key = (Item){.item = s2it(heap_create_name("getReader"))};
    Item get_reader_fn = js_new_function((void*)js_readable_stream_get_reader_stub, 0);
    js_property_set(obj, get_reader_key, get_reader_fn);
    return obj;
}

// WritableStream stub
static Item js_writable_stream_get_writer_stub(void) {
    return js_new_object();
}

extern "C" Item js_writable_stream_new(void) {
    Item obj = js_new_object();
    Item class_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    Item class_val = (Item){.item = s2it(heap_create_name("WritableStream"))};
    js_property_set(obj, class_key, class_val);
    js_class_stamp(obj, JS_CLASS_WRITABLE_STREAM);  // A3-T3b
    Item get_writer_key = (Item){.item = s2it(heap_create_name("getWriter"))};
    Item get_writer_fn = js_new_function((void*)js_writable_stream_get_writer_stub, 0);
    js_property_set(obj, get_writer_key, get_writer_fn);
    return obj;
}