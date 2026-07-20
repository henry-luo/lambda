// js_props.cpp — ES2020 property-model abstract operations (Stage A1)
//
// Migration note: this file collects the canonical kernels of the JS [[Get]]
// / [[Set]] / [[Delete]] / [[HasProperty]] algorithms. Each call site that
// today inlines the IS_ACCESSOR / deleted-sentinel / receiver dispatch is
// being routed through these helpers one at a time, gated by the test262
// baseline + the Stage B property-invariant harness.
//
// First migrated kernel (Stage A1.3a): `js_ordinary_get_own` — the own-property
// lookup with IS_ACCESSOR getter dispatch. Today this exact pattern is
// duplicated 11+ times across js_runtime.cpp; centralizing it kills the
// "we forgot to clear IS_ACCESSOR" / "we forgot to honor the sentinel"
// bug class.

#include "js_props.h"
#include "js_runtime.h"
#include "js_property_attrs.h"
#include "js_state_guards.h"
#include "../lambda-data.hpp"

// js_runtime.cpp internals we need. Public header counterparts:
//   js_map_get_fast_ext   — js_runtime.h
//   js_call_function      — js_runtime.h
//   js_throw_type_error   — js_runtime.h
//   js_find_shape_entry   — js_property_attrs.h
//   js_item_to_accessor_pair, jspd_is_accessor — js_property_attrs.h (inline)
//
// Item helpers (encoding-private to js_runtime.cpp at present): replicate the
// constants we need here. Keep these in lockstep with js_runtime.cpp:188 and
// the Item value layout — these are stable ABI.
#ifndef JS_DELETED_SENTINEL_VAL
#  include "../lambda.h"
#endif

static inline bool js_props_is_deleted_sentinel(Item v) {
    return v.item == JS_DELETED_SENTINEL_VAL;
}

static inline Item js_props_undefined() {
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static inline Item js_props_accessor_result(Item value) {
    return value;
}

// 2-arg heap_create_name lives in transpiler.hpp (defined in lambda-mem.cpp);
// forward-declare here so the kernels below can build name keys.
extern "C++" String* heap_create_name(const char* name, size_t len);
extern void fn_map_set(Item map_item, Item key, Item value);
extern Item _map_read_field(ShapeEntry* field, void* map_data);

// Mirror of JsFuncProps from js_globals.cpp / js_runtime.cpp — used here to
// reach `properties_map`. Layout MUST stay in sync.
struct JsFuncPropsView_props {
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

// Stage E: debug-only invariant assertions. Compiled out under NDEBUG.
// Three classes of invariants the property-model kernels enforce:
//   (E1) Key well-formedness: callers must pass a non-NULL name buffer
//        with non-negative length. Empty-string keys ARE legal in JS
//        (`obj[""]`), so we only forbid NULL/negative-length.
//   (E2) Sentinel exclusivity: if the slot is the deleted sentinel, the
//        shape entry must NOT also be marked IS_ACCESSOR. The two states
//        are mutually exclusive — IS_ACCESSOR clearance is part of delete.
//   (E3) Accessor slot consistency: if shape says IS_ACCESSOR, the slot
//        value must decode to a non-null JsAccessorPair*.
#ifndef NDEBUG
#  include <cassert>
#  define JS_PROPS_ASSERT_KEY(name, len) \
       assert((name) != NULL && (len) >= 0)
#  define JS_PROPS_ASSERT_NOT_DEL_ACCESSOR(slot, se) \
       assert(!(js_props_is_deleted_sentinel(slot) && (se) && jspd_is_accessor(se)))
#  define JS_PROPS_ASSERT_ACCESSOR_PAIR(slot, se) \
       assert(!((se) && jspd_is_accessor(se)) || \
              js_item_to_accessor_pair(slot) != NULL)
#else
#  define JS_PROPS_ASSERT_KEY(name, len)            ((void)0)
#  define JS_PROPS_ASSERT_NOT_DEL_ACCESSOR(slot, se) ((void)0)
#  define JS_PROPS_ASSERT_ACCESSOR_PAIR(slot, se)    ((void)0)
#endif

static Map* js_props_storage_map(Item object) {
    TypeId t = get_type_id(object);
    if (t == LMD_TYPE_MAP) return object.map;
    if (t == LMD_TYPE_ARRAY) {
        Array* arr = object.array;
        return js_array_props(arr);
    }
    if (t == LMD_TYPE_FUNC) {
        JsFuncPropsView_props* fn = (JsFuncPropsView_props*)object.function;
        if (!fn || fn->properties_map.item == 0) return NULL;
        if (get_type_id(fn->properties_map) != LMD_TYPE_MAP) return NULL;
        return fn->properties_map.map;
    }
    return NULL;
}

extern "C" JsShapeSlotStatus js_own_shape_slot_status(Item object,
                                                        const char* name,
                                                        int name_len,
                                                        Item* out_slot,
                                                        ShapeEntry** out_se) {
    JS_PROPS_ASSERT_KEY(name, name_len);
    if (out_slot) *out_slot = ItemNull;
    if (out_se) *out_se = NULL;

    Map* m = js_props_storage_map(object);
    if (!m) return JS_SHAPE_SLOT_ABSENT;

    ShapeEntry* se = js_find_shape_entry(object, name, name_len);
    if (out_se) *out_se = se;

    if (se && m->data && se->byte_offset >= 0 &&
        se->byte_offset <= (int64_t)m->data_cap - (int64_t)sizeof(void*)) {
        if (get_type_id(object) == LMD_TYPE_MAP &&
                map_ctor_offset_is_reserved(m, se->byte_offset)) {
            // Preallocated constructor storage is not an own property before
            // its source assignment executes.
            return JS_SHAPE_SLOT_ABSENT;
        }
        Item slot = _map_read_field(se, m->data);
        if (out_slot) *out_slot = slot;
        if (jspd_is_deleted(se)) return JS_SHAPE_SLOT_DELETED;
        if (js_props_is_deleted_sentinel(slot)) return JS_SHAPE_SLOT_DELETED;
        if (jspd_is_accessor(se)) return JS_SHAPE_SLOT_ACCESSOR;
        return JS_SHAPE_SLOT_DATA;
    }

    bool found = false;
    Item slot = js_map_get_fast_ext(m, name, name_len, &found);
    if (out_slot) *out_slot = slot;

    if (se && jspd_is_deleted(se)) return JS_SHAPE_SLOT_DELETED;
    if (!found) return JS_SHAPE_SLOT_ABSENT;
    if (js_props_is_deleted_sentinel(slot)) return JS_SHAPE_SLOT_DELETED;
    if (se && jspd_is_accessor(se)) return JS_SHAPE_SLOT_ACCESSOR;
    return JS_SHAPE_SLOT_DATA;
}

extern "C" JsOwnGetStatus js_ordinary_get_own(Item object, Item key,
                                              Item Receiver, Item* out_value) {
    // Caller is responsible for ensuring `key` has been canonicalized. String
    // and symbol keys use the central shape/slot status helper so MAP storage,
    // FUNC properties_map storage, and ARRAY companion-map storage share the
    // same deleted/accessor rules.
    TypeId kt = key._type_id;
    if (kt == LMD_TYPE_STRING || kt == LMD_TYPE_SYMBOL) {
        const char* kc = key.get_chars();
        int kl = (int)key.get_len();
        Item slot = ItemNull;
        ShapeEntry* se = NULL;
        JsShapeSlotStatus status = js_own_shape_slot_status(object, kc, kl, &slot, &se);
        if (status == JS_SHAPE_SLOT_ABSENT) return JS_OWN_NOT_FOUND;
        if (status == JS_SHAPE_SLOT_DELETED) return JS_OWN_DELETED;
        if (status == JS_SHAPE_SLOT_ACCESSOR) {
            JsAccessorPair* pair = js_item_to_accessor_pair(slot);
            if (pair && pair->getter.item != ItemNull.item) {
                Item this_val = (Receiver.item ? Receiver : object);
                *out_value = js_props_accessor_result(js_call_function(pair->getter, this_val, NULL, 0));
                return JS_OWN_READY;
            }
            // Setter-only accessor:
            //  - private field (#x): TypeError per ES §PrivateFieldGet.
            //  - public         : returns undefined per ES §9.1.8.1.
            if (kl > 10 && memcmp(kc, "__private_", 10) == 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "'#%.*s' was defined without a getter",
                         kl - 10, kc + 10);
                *out_value = js_throw_type_error(msg);
                return JS_OWN_READY;
            }
            *out_value = js_props_undefined();
            return JS_OWN_READY;
        }
        *out_value = slot;
        return JS_OWN_READY;
    }

    Map* m = object.map;
    Item slot = map_get(m, key);
    bool own_found = (slot.item != ItemNull.item);
    if (!own_found) return JS_OWN_NOT_FOUND;
    if (js_props_is_deleted_sentinel(slot)) return JS_OWN_DELETED;
    *out_value = slot;
    return JS_OWN_READY;
}

// External: defined in js_property_attrs.cpp; walks own + proto chain looking
// for an IS_ACCESSOR pair under `name`. Returns nullptr if no own/inherited
// accessor pair is found, or terminates early at an own data property.
extern "C" JsAccessorPair* js_find_accessor_pair_inheritable(Item obj,
                                                              const char* name,
                                                              int name_len);

extern "C" JsSetterDispatchStatus js_ordinary_set_via_accessor(Item object,
                                                                const char* name,
                                                                int name_len,
                                                                Item value,
                                                                Item Receiver) {
    JsAccessorPair* ap = js_find_accessor_pair_inheritable(object, name, name_len);
    if (!ap) return JS_SET_NOT_FOUND;

    if (ap->setter.item != ItemNull.item &&
        get_type_id(ap->setter) == LMD_TYPE_FUNC) {
        Item args[1] = { value };
        Item this_val = (Receiver.item ? Receiver : object);
        js_call_function(ap->setter, this_val, args, 1);
        return JS_SET_DISPATCHED;
    }
    return JS_SET_NO_SETTER;
}

extern "C" JsOwnDescKind js_ordinary_get_own_descriptor(Item object,
                                                         const char* name,
                                                         int name_len,
                                                         JsAccessorPair** out_pair,
                                                         Item* out_value) {
    JS_PROPS_ASSERT_KEY(name, name_len);
    if (out_pair) *out_pair = NULL;
    Item slot = ItemNull;
    ShapeEntry* se = NULL;
    JsShapeSlotStatus status = js_own_shape_slot_status(object, name, name_len, &slot, &se);
    if (status == JS_SHAPE_SLOT_ABSENT) return JS_DESC_NONE;
    if (status == JS_SHAPE_SLOT_DELETED) return JS_DESC_DELETED;
    JS_PROPS_ASSERT_NOT_DEL_ACCESSOR(slot, se);
    JS_PROPS_ASSERT_ACCESSOR_PAIR(slot, se);
    if (status == JS_SHAPE_SLOT_ACCESSOR) {
        JsAccessorPair* pair = js_item_to_accessor_pair(slot);
        if (pair) {
            if (out_pair) *out_pair = pair;
            return JS_DESC_ACCESSOR;
        }
        // Shape says accessor but slot isn't a pair — treat as data
        // (defensive; should not happen with intact invariants).
    }
    if (out_value) *out_value = slot;
    return JS_DESC_DATA;
}

// Stage A1.8: own-only HasProperty boolean kernel.
//
// Spec: ES §10.1.5.1 OrdinaryGetOwnProperty step "If desc is undefined,
// return undefined". This collapses the slot+sentinel+IS_ACCESSOR detection
// into a single boolean, used by `js_has_own_property` MAP/FUNC branches and
// by callers that only need to know "is there an own property here?".
//
// Returns true iff there is an own MAP/FUNC/ARRAY companion-map slot and it is
// not tombstoned by shape bit or a retained raw hole value. IS_ACCESSOR slots
// count as present (the pair itself is the descriptor).
extern "C" bool js_ordinary_has_own(Item object, const char* name, int name_len) {
    JS_PROPS_ASSERT_KEY(name, name_len);
    JsShapeSlotStatus status = js_own_shape_slot_status(object, name, name_len, NULL, NULL);
    return status == JS_SHAPE_SLOT_DATA || status == JS_SHAPE_SLOT_ACCESSOR;
}

// Stage A1.8b: tri-state own-slot status — see header for contract.
extern "C" JsOwnSlotStatus js_ordinary_own_status(Item object, const char* name, int name_len) {
    JS_PROPS_ASSERT_KEY(name, name_len);
    JsShapeSlotStatus status = js_own_shape_slot_status(object, name, name_len, NULL, NULL);
    if (status == JS_SHAPE_SLOT_DELETED) return JS_HAS_DELETED;
    if (status == JS_SHAPE_SLOT_DATA || status == JS_SHAPE_SLOT_ACCESSOR) return JS_HAS_PRESENT;
    return JS_HAS_ABSENT;
}

// Stage A1.9: own + proto-chain HasProperty (no proxy / builtin fallback).
extern "C" bool js_ordinary_has_property(Item object, const char* name, int name_len) {
    JS_PROPS_ASSERT_KEY(name, name_len);
    Item cur = object;
    int depth = 0;
    while (cur.item != ItemNull.item && get_type_id(cur) == LMD_TYPE_MAP && depth < 32) {
        if (js_ordinary_has_own(cur, name, name_len)) return true;
        cur = js_get_prototype_of(cur);
        depth++;
    }
    return false;
}

// Stage A1.10: OrdinaryGet — own + proto chain MAP-only kernel.
extern "C" bool js_ordinary_get(Item object, const char* name, int name_len,
                                 Item Receiver, Item* out_value) {
    JS_PROPS_ASSERT_KEY(name, name_len);
    if (!out_value) return false;
    Item key = (Item){.item = s2it(heap_create_name(name, name_len))};
    Item cur = object;
    int depth = 0;
    while (cur.item != ItemNull.item && get_type_id(cur) == LMD_TYPE_MAP && depth < 32) {
        Item val = ItemNull;
        JsOwnGetStatus st = js_ordinary_get_own(cur, key, Receiver, &val);
        if (st == JS_OWN_READY) { *out_value = val; return true; }
        // NOT_FOUND or DELETED — keep walking.
        cur = js_get_prototype_of(cur);
        depth++;
    }
    return false;
}

// Stage A1.11: OrdinarySet — inherited-setter dispatch + own data write.
extern "C" JsSetterDispatchStatus js_ordinary_set(Item object, const char* name, int name_len,
                                                    Item value, Item Receiver) {
    JS_PROPS_ASSERT_KEY(name, name_len);
    JsSetterDispatchStatus st = js_ordinary_set_via_accessor(object, name, name_len, value, Receiver);
    if (st != JS_SET_NOT_FOUND) return st;
    // No inherited accessor — perform own data write on Receiver.
    Item target = (Receiver.item ? Receiver : object);
    if (get_type_id(target) != LMD_TYPE_MAP) return JS_SET_NOT_FOUND;
    Item key = (Item){.item = s2it(heap_create_name(name, name_len))};
    js_property_set(target, key, value);
    return JS_SET_DATA_WRITTEN;
}

extern "C" bool js_shape_mark_deleted_own(Item object, const char* name, int name_len,
                                           bool create_if_missing) {
    JS_PROPS_ASSERT_KEY(name, name_len);
    ShapeEntry* se = js_find_shape_entry(object, name, name_len);
    if (!se && create_if_missing) {
        Item key = (Item){.item = s2it(heap_create_name(name, (size_t)name_len))};
        {
            ScopedSkipAccessorDispatch _skip_guard;
            js_property_set(object, key, js_props_undefined());
        }
        se = js_find_shape_entry(object, name, name_len);
    }
    if (!se) return false;
    if (jspd_is_accessor(se)) {
        js_shape_entry_set_accessor(object, name, name_len, /*is_accessor=*/false);
    }
    js_shape_entry_update_flags(object, name, name_len, 0,
        (uint8_t)(JSPD_NON_WRITABLE | JSPD_NON_ENUMERABLE | JSPD_NON_CONFIGURABLE));
    js_shape_entry_set_deleted(object, name, name_len, /*is_deleted=*/true);
    return true;
}

// Stage A1.12: OrdinaryDelete — own-property delete on LMD_TYPE_MAP.
extern "C" bool js_ordinary_delete(Item object, const char* name, int name_len) {
    JS_PROPS_ASSERT_KEY(name, name_len);
    if (get_type_id(object) != LMD_TYPE_MAP) return true;
    Map* m = object.map;
    Item slot = ItemNull;
    ShapeEntry* se = NULL;
    JsShapeSlotStatus status = js_own_shape_slot_status(object, name, name_len, &slot, &se);
    // Absent or tombstoned: per spec, delete of non-existent property succeeds.
    if (status == JS_SHAPE_SLOT_ABSENT || status == JS_SHAPE_SLOT_DELETED) return true;

    // Non-configurable check (shape-flag-first via existing helper).
    JS_PROPS_ASSERT_NOT_DEL_ACCESSOR(slot, se);
    JS_PROPS_ASSERT_ACCESSOR_PAIR(slot, se);
    if (!js_props_query_configurable(m, se, name, name_len)) return false;

    return js_shape_mark_deleted_own(object, name, name_len, /*create_if_missing=*/false);
}

extern "C" JsResolveFieldStatus js_ordinary_resolve_shape_value(ShapeEntry* e,
                                                                  Map* m,
                                                                  Item receiver,
                                                                  Item* out_value) {
    if (map_ctor_offset_is_reserved(m, e->byte_offset)) return JS_RESOLVE_DELETED;
    Item slot = _map_read_field(e, m->data);
    if (jspd_is_deleted(e)) return JS_RESOLVE_DELETED;
    if (js_props_is_deleted_sentinel(slot)) return JS_RESOLVE_DELETED;

    if (jspd_is_accessor(e)) {
        JsAccessorPair* pair = js_item_to_accessor_pair(slot);
        if (pair && pair->getter.item != ItemNull.item) {
            Item v = js_props_accessor_result(js_call_function(pair->getter, receiver, NULL, 0));
            if (js_check_exception()) return JS_RESOLVE_THREW;
            if (out_value) *out_value = v;
            return JS_RESOLVE_VALUE;
        }
        // Setter-only or empty pair: per ES CopyDataProperties / Object.assign,
        // resolves to undefined and is copied as a data property.
        if (out_value) *out_value = js_props_undefined();
        return JS_RESOLVE_VALUE;
    }
    if (out_value) *out_value = slot;
    return JS_RESOLVE_VALUE;
}

// Synthesize descriptor from (obj, storage_map). `obj` is used for shape
// lookup (which delegates to obj.function for FUNC objects); `m` is the
// underlying Map* used for slot reads. For LMD_TYPE_MAP these are the same;
// for LMD_TYPE_FUNC, `m` is fn->properties_map.map.
static bool js_props_desc_from_storage(Item obj, Map* m,
                                        const char* name, int name_len,
                                        JsPropertyDescriptor* out) {
    if (!m) return false;
    Item slot = ItemNull;
    ShapeEntry* se = NULL;
    JsShapeSlotStatus status = js_own_shape_slot_status(obj, name, name_len, &slot, &se);
    if (status == JS_SHAPE_SLOT_ABSENT || status == JS_SHAPE_SLOT_DELETED) return false;

    if (status == JS_SHAPE_SLOT_ACCESSOR) {
        JsAccessorPair* pair = js_item_to_accessor_pair(slot);
        if (pair) {
            out->flags |= JS_PD_HAS_GET;
            out->getter = (pair->getter.item != ItemNull.item)
                            ? pair->getter : js_props_undefined();
            out->flags |= JS_PD_HAS_SET;
            out->setter = (pair->setter.item != ItemNull.item)
                            ? pair->setter : js_props_undefined();
        }
        out->flags |= JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
        if (jspd_is_enumerable(se))   out->flags |= JS_PD_ENUMERABLE;
        if (jspd_is_configurable(se)) out->flags2 |= 0x01u;
        return true;
    }

    // Phase-5D: legacy __get_X/__set_X probe removed. IS_ACCESSOR own-path
    // above is the sole accessor-detection path. Producers no longer create
    // these magic-key markers (Phase 4 intercept routes them to JsAccessorPair).

    out->flags |= JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE
                | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
    out->value = slot;
    if (se) {
        if (jspd_is_writable(se))     out->flags |= JS_PD_WRITABLE;
        if (jspd_is_enumerable(se))   out->flags |= JS_PD_ENUMERABLE;
        if (jspd_is_configurable(se)) out->flags2 |= 0x01u;
    } else {
        out->flags |= JS_PD_WRITABLE | JS_PD_ENUMERABLE;
        out->flags2 |= 0x01u;
    }
    return true;
}

extern "C" bool js_get_own_property_descriptor(Item object,
                                                const char* name,
                                                int name_len,
                                                JsPropertyDescriptor* out) {
    if (!out) return false;
    *out = (JsPropertyDescriptor){};

    TypeId t = get_type_id(object);
    if (t == LMD_TYPE_MAP) {
        return js_props_desc_from_storage(object, object.map, name, name_len, out);
    }
    if (t == LMD_TYPE_FUNC) {
        // FUNC: storage lives in fn->properties_map.map. Caller is still
        // responsible for the synthetic length/name/prototype intrinsics —
        // those are not stored as own properties.
        JsFuncPropsView_props* fn = (JsFuncPropsView_props*)object.function;
        if (!fn || fn->properties_map.item == 0) return false;
        return js_props_desc_from_storage(object, fn->properties_map.map,
                                           name, name_len, out);
    }
    if (t == LMD_TYPE_ARRAY) {
        // ARRAY: own properties (named keys + numeric-index accessors) live
        // in the companion map. The bare-data slot at arr->items[idx] is
        // synthesized as a data descriptor when no accessor is present.
        Array* arr = object.array;
        if (js_array_has_props(arr)) {
            Map* pm = js_array_props(arr);
            Item pm_item = (Item){.map = pm};
            if (js_props_desc_from_storage(pm_item, pm, name, name_len, out)) {
                return true;
            }
        }
        return false;
    }
    return false;
}

// =============================================================================
// Stage A2.3 — descriptor parser + apply kernel
// =============================================================================

extern "C" void js_func_init_property(Item fn, Item key, Item value);

// 2-arg heap_create_name lives in transpiler.hpp (defined in lambda-mem.cpp);
// forward-declare here to avoid pulling the heavy transpiler header.
extern "C++" String* heap_create_name(const char* name, size_t len);

static inline Item js_props_str(const char* s, int len) {
    return (Item){.item = s2it(heap_create_name(s, (size_t)len))};
}

static void js_props_fill_sparse_accessor_index(Item object,
                                                const char* name,
                                                int name_len,
                                                bool is_accessor_desc) {
    // ES array exotic define: accessor descriptors at sparse numeric indices
    // materialize holes up to the index so later length/iteration paths see
    // the descriptor as an own array property.
    if (!is_accessor_desc || get_type_id(object) != LMD_TYPE_ARRAY ||
        name_len <= 0 || name_len > 10) {
        return;
    }

    bool is_idx = true;
    int64_t idx = 0;
    for (int i = 0; i < name_len; i++) {
        char ch = name[i];
        if (ch < '0' || ch > '9') { is_idx = false; break; }
        idx = idx * 10 + (ch - '0');
    }
    if (!is_idx || (name_len > 1 && name[0] == '0') || idx < 0) return;

    Array* arr = object.array;
    int64_t gap = idx - (int64_t)arr->length;
    if (gap < 0 || gap >= 100000) return;

    Item hole = (Item){.item = JS_DELETED_SENTINEL_VAL};
    while ((int64_t)arr->length <= idx) {
        js_array_push_item_direct(arr, hole);
    }
}

static Item js_props_array_numeric_storage_target(Item object,
                                                  const char* name,
                                                  int name_len) {
    if (name_len <= 0) return ItemNull;
    bool is_numeric_index = true;
    if (name_len == 1 && name[0] == '0') {
        is_numeric_index = true;
    } else if (name[0] >= '1' && name[0] <= '9') {
        for (int i = 1; i < name_len; i++) {
            if (name[i] < '0' || name[i] > '9') {
                is_numeric_index = false;
                break;
            }
        }
    } else {
        is_numeric_index = false;
    }
    if (!is_numeric_index) return ItemNull;

    if (get_type_id(object) == LMD_TYPE_ARRAY) {
        Array* arr = object.array;
        if (!js_array_has_props(arr)) return ItemNull;
        return (Item){.map = js_array_props(arr)};
    }
    if (get_type_id(object) == LMD_TYPE_MAP && object.map &&
            map_kind_is_array_props(object.map->map_kind)) {
        return object;
    }
    return ItemNull;
}

static Item js_props_array_companion_storage_target(Item object) {
    if (get_type_id(object) == LMD_TYPE_ARRAY) {
        Array* arr = object.array;
        if (!js_array_has_props(arr)) return ItemNull;
        return (Item){.map = js_array_props(arr)};
    }
    if (get_type_id(object) == LMD_TYPE_MAP && object.map &&
            map_kind_is_array_props(object.map->map_kind)) {
        return object;
    }
    return ItemNull;
}

static Item js_props_function_properties_storage_target(Item object) {
    if (get_type_id(object) != LMD_TYPE_FUNC) return ItemNull;
    JsFuncPropsView_props* fn = (JsFuncPropsView_props*)object.function;
    if (!fn || fn->properties_map.item == 0 ||
            get_type_id(fn->properties_map) != LMD_TYPE_MAP) {
        return ItemNull;
    }
    return fn->properties_map;
}

static Item js_props_existing_accessor_storage_target(Item object) {
    Item target = js_props_array_companion_storage_target(object);
    if (target.item != ItemNull.item) return target;
    target = js_props_function_properties_storage_target(object);
    if (target.item != ItemNull.item) return target;
    return object;
}

static bool js_props_store_raw_data_slot(Item target, ShapeEntry* entry, Item value) {
    if (get_type_id(target) != LMD_TYPE_MAP || !target.map || !entry ||
            !target.map->data || entry->byte_offset < 0) {
        return false;
    }

    TypeId value_type = get_type_id(value);
    int value_size = type_info[value_type].byte_size;
    if (value_size <= 0 ||
            entry->byte_offset + value_size > (int64_t)target.map->data_cap) {
        return false;
    }

    void* field_ptr = (char*)target.map->data + entry->byte_offset;
    switch (value_type) {
    case LMD_TYPE_NULL:
        *(void**)field_ptr = NULL;
        break;
    case LMD_TYPE_UNDEFINED:
        *(bool*)field_ptr = false;
        break;
    case LMD_TYPE_BOOL:
        *(bool*)field_ptr = value.bool_val;
        break;
    case LMD_TYPE_INT:
        *(int64_t*)field_ptr = value.get_int56();
        break;
    case LMD_TYPE_INT64:
        *(int64_t*)field_ptr = value.get_int64();
        break;
    case LMD_TYPE_FLOAT:
        *(double*)field_ptr = value.get_double();
        break;
    case LMD_TYPE_DTIME:
        *(DateTime*)field_ptr = value.get_datetime();
        break;
    case LMD_TYPE_STRING:
        *(String**)field_ptr = value.get_safe_string();
        break;
    case LMD_TYPE_SYMBOL:
        *(Symbol**)field_ptr = value.get_safe_symbol();
        break;
    case LMD_TYPE_BINARY:
        *(Binary**)field_ptr = value.get_safe_binary();
        break;
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_RANGE:
    case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT:
        *(Container**)field_ptr = value.container;
        break;
    case LMD_TYPE_FUNC: case LMD_TYPE_VMAP: case LMD_TYPE_DECIMAL:
    case LMD_TYPE_TYPE: case LMD_TYPE_PATH:
        *(void**)field_ptr = (void*)(uintptr_t)(value.item & 0x00FFFFFFFFFFFFFFULL);
        break;
    case LMD_TYPE_ERROR:
        *(void**)field_ptr = NULL;
        break;
    default:
        return false;
    }

    if (value_type != LMD_TYPE_NULL) {
        entry->type = type_info[value_type].type;
    }
    return true;
}

static inline Item js_props_throw_type(const char* msg) {
    Item tn = js_props_str("TypeError", 9);
    Item m  = js_props_str(msg, (int)strlen(msg));
    js_throw_value(js_new_error_with_name(tn, m));
    return ItemNull;
}

extern "C" bool js_descriptor_from_object(Item desc_obj, JsPropertyDescriptor* out) {
    if (!out) return false;
    *out = (JsPropertyDescriptor){};

    // Reject primitive descriptors per ES §6.2.5.5 step 1 (Type(Obj) is Object).
    TypeId dt = get_type_id(desc_obj);
    if (dt != LMD_TYPE_MAP && dt != LMD_TYPE_FUNC &&
        dt != LMD_TYPE_ARRAY && dt != LMD_TYPE_ELEMENT) {
        js_props_throw_type("Property description must be an object");
        return false;
    }

    Item k_value     = js_props_str("value",        5);
    Item k_writable  = js_props_str("writable",     8);
    Item k_get       = js_props_str("get",          3);
    Item k_set       = js_props_str("set",          3);
    Item k_enum      = js_props_str("enumerable",  10);
    Item k_config    = js_props_str("configurable",12);

    bool has_val   = it2b(js_in(k_value,    desc_obj));
    bool has_wri   = it2b(js_in(k_writable, desc_obj));
    bool has_get   = it2b(js_in(k_get,      desc_obj));
    bool has_set   = it2b(js_in(k_set,      desc_obj));
    bool has_enum  = it2b(js_in(k_enum,     desc_obj));
    bool has_cfg   = it2b(js_in(k_config,   desc_obj));

    // ES §6.2.5.4 step 9: mixed accessor + data → TypeError.
    if ((has_get || has_set) && (has_val || has_wri)) {
        js_props_throw_type(
            "Invalid property descriptor. Cannot both specify accessors and a value or writable attribute");
        return false;
    }

    if (has_val) {
        out->flags |= JS_PD_HAS_VALUE;
        out->value = js_property_get(desc_obj, k_value);
    }
    if (has_wri) {
        out->flags |= JS_PD_HAS_WRITABLE;
        if (js_is_truthy(js_property_get(desc_obj, k_writable)))
            out->flags |= JS_PD_WRITABLE;
    }
    if (has_get) {
        Item getter = js_property_get(desc_obj, k_get);
        TypeId gt = get_type_id(getter);
        if (gt != LMD_TYPE_FUNC && gt != LMD_TYPE_UNDEFINED) {
            js_props_throw_type("Getter must be a function");
            return false;
        }
        out->flags |= JS_PD_HAS_GET;
        out->getter = (gt == LMD_TYPE_FUNC) ? getter : js_props_undefined();
    }
    if (has_set) {
        Item setter = js_property_get(desc_obj, k_set);
        TypeId st = get_type_id(setter);
        if (st != LMD_TYPE_FUNC && st != LMD_TYPE_UNDEFINED) {
            js_props_throw_type("Setter must be a function");
            return false;
        }
        out->flags |= JS_PD_HAS_SET;
        out->setter = (st == LMD_TYPE_FUNC) ? setter : js_props_undefined();
    }
    if (has_enum) {
        out->flags |= JS_PD_HAS_ENUMERABLE;
        if (js_is_truthy(js_property_get(desc_obj, k_enum)))
            out->flags |= JS_PD_ENUMERABLE;
    }
    if (has_cfg) {
        out->flags |= JS_PD_HAS_CONFIGURABLE;
        if (js_is_truthy(js_property_get(desc_obj, k_config)))
            out->flags2 |= 0x01u;
    }
    return true;
}

extern "C" void js_define_own_property_from_descriptor(Item object,
                                                        const char* name,
                                                        int name_len,
                                                        const JsPropertyDescriptor* pd,
                                                        bool is_new_property,
                                                        bool existing_accessor) {
    if (!pd) return;
    if (name_len < 0 || name_len >= 240) return;

    Item name_item = js_props_str(name, name_len);

    // [[DefineOwnProperty]] — must NOT trigger inherited accessor logic.
    // Stage D: RAII guard restores js_skip_accessor_dispatch on every exit.
    ScopedSkipAccessorDispatch _skip_guard;

    bool is_accessor_desc = js_pd_is_accessor(pd);
    bool was_accessor = false;  // accessor→data conversion track
    Item accessor_data_target = ItemNull;

    // Generic descriptor (no value, no get/set) — pre-create the property
    // with `undefined` if it doesn't already exist. This MUST happen before
    // the attribute section runs so that js_attr_set_* can find or materialize
    // the shape entry and update JSPD_NON_* flags on it.
    bool is_data_desc = (pd->flags & JS_PD_HAS_VALUE) != 0;
    if (!is_accessor_desc && !is_data_desc) {
        if (!it2b(js_has_own_property(object, name_item))) {
            js_property_set(object, name_item, (Item){.item = ITEM_JS_UNDEFINED});
        }
    }

    // ----- Accessor descriptor: install via the IS_ACCESSOR chokepoint.
    if (is_accessor_desc) {
        // Detect array exotic: dense array slots do not carry shape entries, so
        // numeric-index accessors are stored under the digit-string key in the
        // array companion map with IS_ACCESSOR set there.
        bool is_array_exotic = (get_type_id(object) == LMD_TYPE_ARRAY) ||
            (get_type_id(object) == LMD_TYPE_MAP && object.map &&
             map_kind_is_array_props(object.map->map_kind));

        if (is_array_exotic) {
            // Numeric (index) array properties: use the companion map digit
            // entry. Writing the pair into arr->items[idx] would clobber a data
            // slot/hole, so the companion-map shape entry owns IS_ACCESSOR.
            //
            // Non-numeric (named) array properties: route through the IS_ACCESSOR
            // chokepoint just like regular objects. The companion map carries
            // the bare-name shape entry, and js_obj_typemap() returns it for
            // arrays via the reserved props slot, so JSPD_IS_ACCESSOR works end-to-end.
            bool is_numeric_index = false;
            if (name_len > 0) {
                is_numeric_index = true;
                if (name_len == 1 && name[0] == '0') {
                    is_numeric_index = true;
                } else if (name[0] >= '1' && name[0] <= '9') {
                    for (int i = 1; i < name_len; i++) {
                        if (name[i] < '0' || name[i] > '9') {
                            is_numeric_index = false;
                            break;
                        }
                    }
                } else {
                    is_numeric_index = false;
                }
            }

            if (is_numeric_index) {
                // Route numeric-index accessors through the IS_ACCESSOR
                // chokepoint *on the companion map* (not the array itself).
                // Calling js_define_accessor_partial(arr, ...) would recurse into
                // js_property_set(arr, "<idx>", pair) which routes to js_array_set
                // and clobbers arr->items[idx]. Targeting the companion map keeps
                // the bare-name slot under the digit-string key with IS_ACCESSOR
                // on its shape entry.
                Item target;
                if (get_type_id(object) == LMD_TYPE_ARRAY) {
                    Array* arr = object.array;
                    if (!js_array_has_props(arr)) {
                        Item nm = js_new_object();
                        nm.map->map_kind = MAP_KIND_ARRAY_PROPS;
                        js_array_set_props(arr, nm.map);
                    }
                    target = (Item){.map = js_array_props(arr)};
                } else {
                    // object IS the companion map (MAP_KIND_ARRAY_PROPS); use it.
                    target = object;
                }
                if (pd->flags & JS_PD_HAS_GET) {
                    js_define_accessor_partial(target, name_item, pd->getter, /*is_setter*/0, /*attrs*/0);
                }
                if (pd->flags & JS_PD_HAS_SET) {
                    js_define_accessor_partial(target, name_item, pd->setter, /*is_setter*/1, /*attrs*/0);
                }
            } else {
                // Named-key path: IS_ACCESSOR + JsAccessorPair via chokepoint.
                if (pd->flags & JS_PD_HAS_GET) {
                    js_define_accessor_partial(object, name_item, pd->getter, /*is_setter*/0, /*attrs*/0);
                }
                if (pd->flags & JS_PD_HAS_SET) {
                    js_define_accessor_partial(object, name_item, pd->setter, /*is_setter*/1, /*attrs*/0);
                }
            }
        } else {
            // Install getter/setter halves via Scheme B chokepoint.
            if (pd->flags & JS_PD_HAS_GET) {
                js_define_accessor_partial(object, name_item, pd->getter, /*is_setter*/0, /*attrs*/0);
            }
            if (pd->flags & JS_PD_HAS_SET) {
                js_define_accessor_partial(object, name_item, pd->setter, /*is_setter*/1, /*attrs*/0);
            }
        }
    } else if (pd->flags & JS_PD_HAS_VALUE) {
        // ----- Data descriptor: write the value, clearing IS_ACCESSOR if
        //       the previous slot held an accessor pair.
        Item value_storage_target = ItemNull;
        if (!is_new_property) {
            Item accessor_target = existing_accessor ?
                js_props_existing_accessor_storage_target(object) :
                js_props_array_numeric_storage_target(object, name, name_len);
            value_storage_target =
                (accessor_target.item != ItemNull.item) ? accessor_target : object;
            ShapeEntry* se = js_find_shape_entry(value_storage_target, name, name_len);
            if ((se && jspd_is_accessor(se)) || existing_accessor) {
                was_accessor = true;
                accessor_data_target = value_storage_target;
            }
        }

        // Temporarily clear non-writable so js_property_set's writable guard
        // does not block the [[DefineOwnProperty]] write (validation already
        // performed by caller). Probe + clear via the flag helper; restore
        // after the value write only if it was originally set.
        bool had_nw = false;
        if (!is_new_property &&
                js_prop_attrs_fast_path(object, name, name_len, JSPD_NON_WRITABLE) == 0) {
            // [[DefineOwnProperty]] may legally replace the value of an existing
            // configurable/non-writable property while also making it writable.
            // The writable guard in js_property_set is shape-flag aware, so this
            // descriptor kernel must clear the shape bit before writing the value.
            had_nw = true;
            js_attr_set_writable(object, name, name_len, /*writable=*/true);
        }

        if (!is_new_property && value_storage_target.item == ItemNull.item) {
            Item accessor_target = existing_accessor ?
                js_props_existing_accessor_storage_target(object) :
                js_props_array_numeric_storage_target(object, name, name_len);
            value_storage_target =
                (accessor_target.item != ItemNull.item) ? accessor_target : object;
        }
        ShapeEntry* value_se = !is_new_property ?
            js_find_shape_entry(
                (value_storage_target.item != ItemNull.item) ? value_storage_target : object,
                name, name_len) : NULL;
        if (!was_accessor && value_se && jspd_is_accessor(value_se)) {
            accessor_data_target =
                (value_storage_target.item != ItemNull.item) ? value_storage_target : object;
            was_accessor = true;
        }
        if (was_accessor && get_type_id(accessor_data_target) == LMD_TYPE_MAP) {
            ShapeEntry* accessor_value_se = js_find_shape_entry(accessor_data_target, name, name_len);
            if (!js_props_store_raw_data_slot(accessor_data_target, accessor_value_se, pd->value)) {
                js_property_set(accessor_data_target, name_item, pd->value);
            }
            // Clear IS_ACCESSOR only after replacing the JsAccessorPair slot.
            // Attribute probes between the two operations must continue to see
            // the slot as an accessor, not as a real Function data property.
            js_shape_entry_set_accessor(accessor_data_target, name, name_len, /*is_accessor=*/false);
        } else if (!is_new_property && get_type_id(object) == LMD_TYPE_MAP &&
                value_se && !jspd_is_accessor(value_se)) {
            // [[DefineOwnProperty]] performs an internal slot replacement after
            // validation. Do not route existing map properties through ordinary
            // [[Set]], which can still reject on writability/non-extensibility.
            // Guard on a real shape entry so virtual/special properties stay on
            // their established setter path.
            if (jspd_is_deleted(value_se)) {
                js_shape_entry_set_deleted(object, name, name_len, /*is_deleted=*/false);
            }
            fn_map_set(object, name_item, pd->value);
        } else if (get_type_id(object) == LMD_TYPE_FUNC) {
            ShapeEntry* fn_se = js_find_shape_entry(object, name, name_len);
            if (fn_se && jspd_is_deleted(fn_se)) {
                js_shape_entry_set_deleted(object, name, name_len, /*is_deleted=*/false);
            }
            js_func_init_property(object, name_item, pd->value);
        } else {
            js_property_set(object, name_item, pd->value);
        }

        // AT-3: legacy __get_/__set_ marker cleanup retired. Post-AT-1 accessors
        // are stored under the property name with IS_ACCESSOR shape flag; the
        // accessor→data conversion above already cleared the flag.

        if (had_nw) js_attr_set_writable(object, name, name_len, /*writable=*/false);
    }

    // ----- Attribute flags: inverse "non-*" bits on ShapeEntry.
    // Routed through js_attr_set_* helpers. Array index/length attributes are
    // materialized in the companion map before the ShapeEntry flags are set.

    // writable — only for data descriptors (accessor descriptors have no writable bit).
    if (!is_accessor_desc) {
        if (pd->flags & JS_PD_HAS_WRITABLE) {
            bool w = (pd->flags & JS_PD_WRITABLE) != 0;
            js_attr_set_writable(object, name, name_len, w);
        } else if (is_new_property || was_accessor) {
            // ES §6.2.5.5: default for new data property is writable=false.
            // accessor→data conversion without explicit writable also defaults
            // to non-writable (matches original ValidateAndApplyPropertyDescriptor).
            js_attr_set_writable(object, name, name_len, /*writable=*/false);
        }
    }

    if (pd->flags & JS_PD_HAS_ENUMERABLE) {
        bool e = (pd->flags & JS_PD_ENUMERABLE) != 0;
        js_attr_set_enumerable(object, name, name_len, e);
    } else if (is_new_property) {
        js_attr_set_enumerable(object, name, name_len, /*enumerable=*/false);
    }

    if (pd->flags & JS_PD_HAS_CONFIGURABLE) {
        bool c = (pd->flags2 & 0x01u) != 0;
        js_attr_set_configurable(object, name, name_len, c);
    } else if (is_new_property) {
        js_attr_set_configurable(object, name, name_len, /*configurable=*/false);
    }

    js_props_fill_sparse_accessor_index(object, name, name_len, is_accessor_desc);
}
