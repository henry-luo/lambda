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

// =============================================================================
// Stage A2.3 — descriptor parser + apply kernel
// =============================================================================

extern "C" void js_func_init_property(Item fn, Item key, Item value);
extern "C" void  js_defprop_set_marker(Item obj, Item key, Item value);
extern "C" bool js_defprop_has_marker(Item obj, Item key);

// 2-arg heap_create_name lives in transpiler.hpp (defined in lambda-mem.cpp);
// forward-declare here to avoid pulling the heavy transpiler header.
extern "C++" String* heap_create_name(const char* name, size_t len);

static inline Item js_props_str(const char* s, int len) {
    return (Item){.item = s2it(heap_create_name(s, (size_t)len))};
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
                                                        bool is_new_property) {
    if (!pd) return;
    if (name_len < 0 || name_len >= 240) return;

    Item name_item = js_props_str(name, name_len);

    // [[DefineOwnProperty]] — must NOT trigger inherited accessor logic.
    extern bool js_skip_accessor_dispatch;
    bool _prev_skip = js_skip_accessor_dispatch;
    js_skip_accessor_dispatch = true;

    bool is_accessor_desc = js_pd_is_accessor(pd);
    bool was_accessor = false;  // accessor→data conversion track

    // Generic descriptor (no value, no get/set) — pre-create the property
    // with `undefined` if it doesn't already exist. This MUST happen before
    // the attribute markers section runs so that `js_dual_write_marker_flags`
    // can find the shape entry and update JSPD_NON_* flags on it. If we wrote
    // markers first, the marker→shape dual-write would no-op (no entry), and
    // a subsequent value write would create the entry with default flags,
    // losing the attribute settings.
    bool is_data_desc = (pd->flags & JS_PD_HAS_VALUE) != 0;
    if (!is_accessor_desc && !is_data_desc) {
        if (!it2b(js_has_own_property(object, name_item))) {
            js_property_set(object, name_item, (Item){.item = ITEM_JS_UNDEFINED});
        }
    }

    // ----- Accessor descriptor: install via the IS_ACCESSOR chokepoint.
    if (is_accessor_desc) {
        char buf[256];
        snprintf(buf, sizeof(buf), "__nw_%.*s", name_len, name);
        Item nw_k = js_props_str(buf, (int)strlen(buf));

        // Detect array exotic: arrays don't carry shape entries for indexed
        // slots, so Scheme B (IS_ACCESSOR shape flag) cannot apply. Use legacy
        // __get_X/__set_X companion-map markers instead — readers for array
        // index access dispatch via these markers.
        bool is_array_exotic = (get_type_id(object) == LMD_TYPE_ARRAY) ||
            (get_type_id(object) == LMD_TYPE_MAP && object.map &&
             object.map->map_kind == MAP_KIND_ARRAY_PROPS);

        // For converting data→accessor on existing slot, tombstone the data
        // slot first.
        if (get_type_id(object) == LMD_TYPE_MAP && !is_new_property && !is_array_exotic) {
            bool data_found = false;
            js_map_get_fast_ext(object.map, name, name_len, &data_found);
            if (data_found) {
                ShapeEntry* se = js_find_shape_entry(object, name, name_len);
                if (!(se && jspd_is_accessor(se))) {
                    Item del = (Item){.item = JS_DELETED_SENTINEL_VAL};
                    js_property_set(object, name_item, del);
                }
            }
        }

        if (is_array_exotic) {
            // Use js_defprop_set_marker which routes to the array companion
            // map (MAP_KIND_ARRAY_PROPS). Writing directly to LMD_TYPE_ARRAY
            // would mis-route through the array's index storage.
            // Note: we write the descriptor's value verbatim — including
            // explicit-undefined halves. Tombstoning would collapse "explicit
            // undefined accessor half" into "marker absent", and breaks
            // hasOwnProperty for arrays where presence is detected via the
            // companion-map __get_X/__set_X keys.
            char gk_buf[256], sk_buf[256];
            snprintf(gk_buf, sizeof(gk_buf), "__get_%.*s", name_len, name);
            snprintf(sk_buf, sizeof(sk_buf), "__set_%.*s", name_len, name);
            Item gk = js_props_str(gk_buf, (int)strlen(gk_buf));
            Item sk = js_props_str(sk_buf, (int)strlen(sk_buf));
            if (pd->flags & JS_PD_HAS_GET) {
                js_defprop_set_marker(object, gk, pd->getter);
            }
            if (pd->flags & JS_PD_HAS_SET) {
                js_defprop_set_marker(object, sk, pd->setter);
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
        // Conditional clear of stale __nw_ marker.
        if (js_defprop_has_marker(object, nw_k)) {
            js_defprop_set_marker(object, nw_k, (Item){.item = b2it(false)});
        }
    } else if (pd->flags & JS_PD_HAS_VALUE) {
        // ----- Data descriptor: write the value, clearing IS_ACCESSOR if
        //       the previous slot held an accessor pair.
        if (!is_new_property) {
            ShapeEntry* se = js_find_shape_entry(object, name, name_len);
            if (se && jspd_is_accessor(se)) { jspd_set_accessor(se, false); was_accessor = true; }
        }

        // Temporarily clear __nw_ so js_property_set's writable guard does
        // not block the [[DefineOwnProperty]] write (validation already
        // performed by caller).
        char nw_buf[256];
        snprintf(nw_buf, sizeof(nw_buf), "__nw_%.*s", name_len, name);
        Item nw_k = js_props_str(nw_buf, (int)strlen(nw_buf));
        bool had_nw = false;
        if (!is_new_property && it2b(js_has_own_property(object, nw_k))) {
            Item nv = js_property_get(object, nw_k);
            had_nw = js_is_truthy(nv);
            if (had_nw) js_defprop_set_marker(object, nw_k, (Item){.item = b2it(false)});
        }

        if (get_type_id(object) == LMD_TYPE_FUNC) {
            js_func_init_property(object, name_item, pd->value);
        } else {
            js_property_set(object, name_item, pd->value);
        }

        // Clean up legacy __get_/__set_ markers when converting accessor→data.
        char gk_buf[256], sk_buf[256];
        snprintf(gk_buf, sizeof(gk_buf), "__get_%.*s", name_len, name);
        snprintf(sk_buf, sizeof(sk_buf), "__set_%.*s", name_len, name);
        Item gk = js_props_str(gk_buf, (int)strlen(gk_buf));
        Item sk = js_props_str(sk_buf, (int)strlen(sk_buf));
        Item del = (Item){.item = JS_DELETED_SENTINEL_VAL};
        // Probe via js_has_own_property (handles array companion-map indirection).
        if (it2b(js_has_own_property(object, gk))) { js_defprop_set_marker(object, gk, del); was_accessor = true; }
        if (it2b(js_has_own_property(object, sk))) { js_defprop_set_marker(object, sk, del); was_accessor = true; }

        if (had_nw) js_defprop_set_marker(object, nw_k, (Item){.item = b2it(true)});
    }

    // ----- Attribute markers: __nw_, __nc_, __ne_ (inverse "non-*" bits).
    char attr_buf[256];

    // writable — only for data descriptors (accessor descriptors have no writable bit).
    if (!is_accessor_desc) {
        if (pd->flags & JS_PD_HAS_WRITABLE) {
            bool w = (pd->flags & JS_PD_WRITABLE) != 0;
            snprintf(attr_buf, sizeof(attr_buf), "__nw_%.*s", name_len, name);
            Item k = js_props_str(attr_buf, (int)strlen(attr_buf));
            js_defprop_set_marker(object, k, (Item){.item = b2it(!w)});
        } else if (is_new_property || was_accessor) {
            // ES §6.2.5.5: default for new data property is writable=false.
            // accessor→data conversion without explicit writable also defaults
            // to non-writable (matches original ValidateAndApplyPropertyDescriptor).
            snprintf(attr_buf, sizeof(attr_buf), "__nw_%.*s", name_len, name);
            Item k = js_props_str(attr_buf, (int)strlen(attr_buf));
            js_defprop_set_marker(object, k, (Item){.item = b2it(true)});
        }
    }

    if (pd->flags & JS_PD_HAS_ENUMERABLE) {
        bool e = (pd->flags & JS_PD_ENUMERABLE) != 0;
        snprintf(attr_buf, sizeof(attr_buf), "__ne_%.*s", name_len, name);
        Item k = js_props_str(attr_buf, (int)strlen(attr_buf));
        js_defprop_set_marker(object, k, (Item){.item = b2it(!e)});
    } else if (is_new_property) {
        snprintf(attr_buf, sizeof(attr_buf), "__ne_%.*s", name_len, name);
        Item k = js_props_str(attr_buf, (int)strlen(attr_buf));
        js_defprop_set_marker(object, k, (Item){.item = b2it(true)});
    }

    if (pd->flags & JS_PD_HAS_CONFIGURABLE) {
        bool c = (pd->flags2 & 0x01u) != 0;
        snprintf(attr_buf, sizeof(attr_buf), "__nc_%.*s", name_len, name);
        Item k = js_props_str(attr_buf, (int)strlen(attr_buf));
        js_defprop_set_marker(object, k, (Item){.item = b2it(!c)});
    } else if (is_new_property) {
        snprintf(attr_buf, sizeof(attr_buf), "__nc_%.*s", name_len, name);
        Item k = js_props_str(attr_buf, (int)strlen(attr_buf));
        js_defprop_set_marker(object, k, (Item){.item = b2it(true)});
    }

    js_skip_accessor_dispatch = _prev_skip;
}
