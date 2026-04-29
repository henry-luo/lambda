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

extern "C" JsOwnGetStatus js_ordinary_get_own(Item object, Item key,
                                              Item Receiver, Item* out_value) {
    // Caller is responsible for ensuring `object` is LMD_TYPE_MAP and `key`
    // has been canonicalized. We still tolerate non-string keys (fallback to
    // map_get) for robustness during migration.
    Map* m = object.map;
    bool own_found = false;
    Item slot = ItemNull;

    TypeId kt = key._type_id;
    if (kt == LMD_TYPE_STRING || kt == LMD_TYPE_SYMBOL) {
        const char* ks = key.get_chars();
        int kl = (int)key.get_len();
        slot = js_map_get_fast_ext(m, ks, kl, &own_found);
    } else {
        slot = map_get(m, key);
        own_found = (slot.item != ItemNull.item);
    }

    if (!own_found) return JS_OWN_NOT_FOUND;

    // Deleted-sentinel: caller should fall through to prototype chain (and
    // remember the deletion for top-of-chain Object.prototype semantics).
    if (js_props_is_deleted_sentinel(slot)) return JS_OWN_DELETED;

    // IS_ACCESSOR dispatch — only string/symbol keys can carry shape entries.
    if (kt == LMD_TYPE_STRING || kt == LMD_TYPE_SYMBOL) {
        ShapeEntry* se = js_find_shape_entry(object, key.get_chars(),
                                             (int)key.get_len());
        if (se && jspd_is_accessor(se)) {
            JsAccessorPair* pair = js_item_to_accessor_pair(slot);
            if (pair && pair->getter.item != ItemNull.item) {
                Item this_val = (Receiver.item ? Receiver : object);
                *out_value = js_call_function(pair->getter, this_val, NULL, 0);
                return JS_OWN_READY;
            }
            // Setter-only accessor:
            //  - private field (#x): TypeError per ES §PrivateFieldGet.
            //  - public         : returns undefined per ES §9.1.8.1.
            const char* kc = key.get_chars();
            int kl = (int)key.get_len();
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
    }

    // Plain data property.
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
    if (out_pair) *out_pair = NULL;
    if (get_type_id(object) != LMD_TYPE_MAP) return JS_DESC_NONE;

    bool found = false;
    Item slot = js_map_get_fast_ext(object.map, name, name_len, &found);
    if (!found) return JS_DESC_NONE;
    if (js_props_is_deleted_sentinel(slot)) return JS_DESC_DELETED;

    ShapeEntry* se = js_find_shape_entry(object, name, name_len);
    if (se && jspd_is_accessor(se)) {
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

// External: defined in lambda-data-runtime.cpp (C++ linkage).
extern Item _map_read_field(ShapeEntry* field, void* map_data);

extern "C" JsResolveFieldStatus js_ordinary_resolve_shape_value(ShapeEntry* e,
                                                                  Map* m,
                                                                  Item receiver,
                                                                  Item* out_value) {
    Item slot = _map_read_field(e, m->data);
    if (js_props_is_deleted_sentinel(slot)) return JS_RESOLVE_DELETED;

    if (jspd_is_accessor(e)) {
        JsAccessorPair* pair = js_item_to_accessor_pair(slot);
        if (pair && pair->getter.item != ItemNull.item) {
            Item v = js_call_function(pair->getter, receiver, NULL, 0);
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

// Synthesize descriptor from (obj, storage_map). `obj` is used for shape
// lookup (which delegates to obj.function for FUNC objects); `m` is the
// underlying Map* used for slot reads. For LMD_TYPE_MAP these are the same;
// for LMD_TYPE_FUNC, `m` is fn->properties_map.map.
static bool js_props_desc_from_storage(Item obj, Map* m,
                                        const char* name, int name_len,
                                        JsPropertyDescriptor* out) {
    if (!m) return false;
    ShapeEntry* se = js_find_shape_entry(obj, name, name_len);

    bool slot_found = false;
    Item slot = js_map_get_fast_ext(m, name, name_len, &slot_found);

    if (slot_found && js_props_is_deleted_sentinel(slot)) return false;

    if (slot_found && se && jspd_is_accessor(se)) {
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

    if (name_len > 0 && name_len < 240) {
        char buf[256];
        bool g_found = false, s_found = false;
        Item getter = ItemNull, setter = ItemNull;

        snprintf(buf, sizeof(buf), "__get_%.*s", name_len, name);
        getter = js_map_get_fast_ext(m, buf, (int)strlen(buf), &g_found);
        if (g_found && js_props_is_deleted_sentinel(getter)) g_found = false;

        snprintf(buf, sizeof(buf), "__set_%.*s", name_len, name);
        setter = js_map_get_fast_ext(m, buf, (int)strlen(buf), &s_found);
        if (s_found && js_props_is_deleted_sentinel(setter)) s_found = false;

        if (g_found || s_found) {
            out->flags |= JS_PD_HAS_GET | JS_PD_HAS_SET;
            out->getter = (g_found && get_type_id(getter) == LMD_TYPE_FUNC)
                            ? getter : js_props_undefined();
            out->setter = (s_found && get_type_id(setter) == LMD_TYPE_FUNC)
                            ? setter : js_props_undefined();
            out->flags |= JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
            if (se) {
                if (jspd_is_enumerable(se))   out->flags |= JS_PD_ENUMERABLE;
                if (jspd_is_configurable(se)) out->flags2 |= 0x01u;
            } else {
                bool nc_f = false, ne_f = false;
                snprintf(buf, sizeof(buf), "__nc_%.*s", name_len, name);
                Item nc_v = js_map_get_fast_ext(m, buf, (int)strlen(buf), &nc_f);
                snprintf(buf, sizeof(buf), "__ne_%.*s", name_len, name);
                Item ne_v = js_map_get_fast_ext(m, buf, (int)strlen(buf), &ne_f);
                if (!(ne_f && js_is_truthy(ne_v))) out->flags  |= JS_PD_ENUMERABLE;
                if (!(nc_f && js_is_truthy(nc_v))) out->flags2 |= 0x01u;
            }
            return true;
        }
    }

    if (!slot_found) return false;

    out->flags |= JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE
                | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
    out->value = slot;
    if (se) {
        if (jspd_is_writable(se))     out->flags |= JS_PD_WRITABLE;
        if (jspd_is_enumerable(se))   out->flags |= JS_PD_ENUMERABLE;
        if (jspd_is_configurable(se)) out->flags2 |= 0x01u;
    } else if (name_len > 0 && name_len < 240) {
        char buf[256];
        bool nw_f = false, nc_f = false, ne_f = false;
        snprintf(buf, sizeof(buf), "__nw_%.*s", name_len, name);
        Item nw_v = js_map_get_fast_ext(m, buf, (int)strlen(buf), &nw_f);
        snprintf(buf, sizeof(buf), "__nc_%.*s", name_len, name);
        Item nc_v = js_map_get_fast_ext(m, buf, (int)strlen(buf), &nc_f);
        snprintf(buf, sizeof(buf), "__ne_%.*s", name_len, name);
        Item ne_v = js_map_get_fast_ext(m, buf, (int)strlen(buf), &ne_f);
        if (!(nw_f && js_is_truthy(nw_v))) out->flags  |= JS_PD_WRITABLE;
        if (!(ne_f && js_is_truthy(ne_v))) out->flags  |= JS_PD_ENUMERABLE;
        if (!(nc_f && js_is_truthy(nc_v))) out->flags2 |= 0x01u;
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
    return false;
}
