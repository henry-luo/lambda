// js_coerce.cpp — Lambda JS coercion kernels (J39-1).
//
// Implements:
//   ES §7.1.1   ToPrimitive(input, hint)
//   ES §7.1.1.1 OrdinaryToPrimitive(O, hint)
//
// See js_coerce.h and vibe/jube/Transpile_Js39.md §J39-1.

#include "js_coerce.h"
#include "js_runtime.h"
#include "../lambda-data.hpp"

// Engine globals/functions used here are declared in js_runtime.h. The
// 2-arg heap_create_name(const char*, size_t) overload lives in
// lambda-mem.cpp but is only declared inside transpiler.hpp; pull a local
// forward decl to avoid dragging the transpiler header in.
String* heap_create_name(const char* name, size_t len);
Map* js_resolve_object_prototype();

// Cached interned symbol/method-name keys. heap_create_name interns into
// the name pool, so these are stable for the lifetime of the heap.
static inline Item k_sym_to_primitive(void) {
    return (Item){.item = s2it(heap_create_name("__sym_2", 7))};
}
static inline Item k_value_of(void) {
    return (Item){.item = s2it(heap_create_name("valueOf", 7))};
}
static inline Item k_to_string(void) {
    return (Item){.item = s2it(heap_create_name("toString", 8))};
}

static inline bool is_object_type(TypeId t) {
    return t == LMD_TYPE_MAP || t == LMD_TYPE_ARRAY ||
           t == LMD_TYPE_FUNC || t == LMD_TYPE_ELEMENT;
}

// Per ES §7.1.1 step 4.b: a valid ToPrimitive return is *not* an object.
// We treat the four object TypeIds above as objects for this check.
static inline bool result_is_object(TypeId rt) {
    return is_object_type(rt);
}

static inline bool js_coerce_is_bigint(Item value) {
    if (get_type_id(value) != LMD_TYPE_DECIMAL) return false;
    Decimal* dec = (Decimal*)(value.item & 0x00FFFFFFFFFFFFFF);
    return dec && dec->unlimited == DECIMAL_BIGINT;
}

static bool js_proxy_has_get_trap(Item value) {
    JsProxyData* pd = js_get_proxy_data(value);
    if (!pd) return true;
    Item handler = (Item){.item = pd->handler};
    Item get_key = (Item){.item = s2it(heap_create_name("get", 3))};
    Item trap = js_property_get(handler, get_key);
    if (js_check_exception()) return true;
    TypeId trap_type = get_type_id(trap);
    return trap.item != ItemNull.item && trap_type != LMD_TYPE_UNDEFINED && trap_type != LMD_TYPE_NULL;
}

static bool js_is_class_constructor_map_for_coerce(Item value) {
    if (get_type_id(value) != LMD_TYPE_MAP || !value.map) return false;
    bool has_instance_proto = false;
    js_map_get_fast_ext(value.map, "__instance_proto__", 18, &has_instance_proto);
    if (has_instance_proto) return true;
    bool has_ctor = false;
    js_map_get_fast_ext(value.map, "__ctor__", 8, &has_ctor);
    return has_ctor;
}

static bool js_is_callable_proxy_target_for_coerce(Item value) {
    return get_type_id(value) == LMD_TYPE_FUNC || js_is_class_constructor_map_for_coerce(value);
}

extern "C" Item js_to_primitive(Item value, JsHint hint) {
    TypeId vt = get_type_id(value);
    if (!is_object_type(vt)) return value;

    const char* hint_str =
        (hint == JS_HINT_NUMBER) ? "number" :
        (hint == JS_HINT_STRING) ? "string" : "default";

    // Wrapper fast-path: only for plain MAP, only when no custom override
    // (valueOf / toString / @@toPrimitive) shadows the boxed primitive.
    // Preserves the long-standing js_op_to_primitive behavior — exiting
    // the wrapper here avoids a redundant valueOf trampoline through the
    // boxed-primitive prototype.
    if (vt == LMD_TYPE_MAP) {
        bool own_pv = false;
        Item pv = js_map_get_fast_ext(value.map, "__primitiveValue__", 18, &own_pv);
        bool pv_is_symbol = get_type_id(pv) == LMD_TYPE_INT && it2i(pv) <= -(int64_t)JS_SYMBOL_BASE;
        if (own_pv && !js_coerce_is_bigint(pv) && !pv_is_symbol) {
            bool has_vo = false, has_ts = false, has_tp = false;
            js_map_get_fast_ext(value.map, "valueOf", 7, &has_vo);
            js_map_get_fast_ext(value.map, "toString", 8, &has_ts);
            js_map_get_fast_ext(value.map, "__sym_2", 7, &has_tp);
            if (!has_vo && !has_ts && !has_tp) return pv;
        }
    }

    // Step 2: @@toPrimitive lookup (prototype-chain walk via js_property_get).
    Item to_prim = js_property_get(value, k_sym_to_primitive());
    if (js_check_exception()) return ItemNull;
    TypeId tp_type = get_type_id(to_prim);
    bool tp_present = (to_prim.item != ItemNull.item &&
                       tp_type != LMD_TYPE_UNDEFINED &&
                       tp_type != LMD_TYPE_NULL);

    if (tp_present && tp_type != LMD_TYPE_FUNC) {
        // ES §7.1.1 step 2.b.i: callable check.
        js_throw_type_error("@@toPrimitive is not a function");
        return ItemNull;
    }

    if (tp_present) {
        // Note: heap_create_name(hint_str) interns; cheap and safe to
        // re-allocate per call (same address as previous calls with the
        // same hint string).
        Item hint_item = (Item){.item = s2it(heap_create_name(hint_str))};
        Item args[1] = { hint_item };
        Item result = js_call_function(to_prim, value, args, 1);
        if (js_check_exception()) return ItemNull;
        if (result_is_object(get_type_id(result))) {
            js_throw_type_error("Cannot convert object to primitive value");
            return ItemNull;
        }
        return result;
    }

    if (vt == LMD_TYPE_MAP) {
        bool raw_proto_found = false;
        Item raw_proto = js_map_get_fast_ext(value.map, "__proto__", 9, &raw_proto_found);
        Item proto = raw_proto_found ? raw_proto : js_get_prototype(value);
        TypeId proto_type = get_type_id(proto);
        bool null_proto = proto.item == ItemNull.item || proto.item == ITEM_JS_UNDEFINED ||
            proto_type == LMD_TYPE_NULL || proto_type == LMD_TYPE_UNDEFINED;
        Map* object_proto = js_resolve_object_prototype();
        if (raw_proto_found && null_proto && value.map != object_proto &&
            !js_map_kind_uses_default_object_to_primitive(value.map->map_kind)) {
            bool has_vo = false, has_ts = false;
            js_map_get_fast_ext(value.map, "valueOf", 7, &has_vo);
            js_map_get_fast_ext(value.map, "toString", 8, &has_ts);
            if (!has_vo && !has_ts) {
                js_throw_type_error("Cannot convert object to primitive value");
                return ItemNull;
            }
        }
    }

    if (vt == LMD_TYPE_MAP && js_is_proxy(value) && !js_proxy_has_get_trap(value)) {
        Item target = js_proxy_get_target(value);
        if (js_is_callable_proxy_target_for_coerce(target)) {
            Item fn = js_lookup_builtin_method(LMD_TYPE_FUNC, "toString", 8);
            if (fn.item != ItemNull.item && get_type_id(fn) == LMD_TYPE_FUNC) {
                Item result = js_call_function(fn, value, NULL, 0);
                if (js_check_exception()) return ItemNull;
                if (!result_is_object(get_type_id(result))) return result;
            }
        }
    }

    // Step 3: OrdinaryToPrimitive — method order depends on hint.
    // For "string" hint: toString, then valueOf.
    // For "default" / "number" hint: valueOf, then toString.
    Item method_keys[2];
    if (hint == JS_HINT_STRING) {
        method_keys[0] = k_to_string();
        method_keys[1] = k_value_of();
    } else {
        method_keys[0] = k_value_of();
        method_keys[1] = k_to_string();
    }

    for (int i = 0; i < 2; i++) {
        Item fn = js_property_get(value, method_keys[i]);
        if (js_check_exception()) return ItemNull;
        if (fn.item == ItemNull.item || get_type_id(fn) != LMD_TYPE_FUNC) continue;
        Item result = js_call_function(fn, value, NULL, 0);
        if (js_check_exception()) return ItemNull;
        if (!result_is_object(get_type_id(result))) {
            return result;
        }
    }

    // Step 4: every callable method returned an object → TypeError.
    // v28: DOM/CSSOM elements have non-callable toString/valueOf placeholders
    // (returning boolean ITEM_TRUE for feature detection). Fall through to
    // default string conversion instead of throwing.
    if (vt == LMD_TYPE_MAP && value.map &&
        js_map_kind_uses_default_object_to_primitive(value.map->map_kind)) {
        return (Item){.item = s2it(heap_create_name("[object Object]"))};
    }
    js_throw_type_error("Cannot convert object to primitive value");
    return ItemNull;
}
