// js_coerce.cpp — Lambda JS coercion kernels (J39-1).
//
// Implements:
//   ES §7.1.1   ToPrimitive(input, hint)
//   ES §7.1.1.1 OrdinaryToPrimitive(O, hint)
//
// See js_coerce.h and vibe/jube/Transpile_Js39.md §J39-1.

#include "js_coerce.h"
#include "js_runtime.h"
#include "js_object_meta.h"
#include "../lambda-data.hpp"

// Engine globals/functions used here are declared in js_runtime.h. The
// 2-arg heap_create_name(const char*, size_t) overload lives in
// lambda-mem.cpp but is only declared inside transpiler.hpp; pull a local
// forward decl to avoid dragging the transpiler header in.
String* heap_create_name(const char* name, size_t len);
Map* js_resolve_object_prototype();

// Cached interned method-name keys. Well-known Symbols use the generated
// runtime records below because their diagnostic spelling is not identity.
static inline Item k_sym_to_primitive(void) {
    return js_well_known_symbol_key(2);
}
static inline Item k_value_of(void) {
    return js_name_item("valueOf", 7);
}
static inline Item k_to_string(void) {
    return js_name_item("toString", 8);
}

static inline bool is_object_value(Item value) {
    TypeId t = get_type_id(value);
    return t == LMD_TYPE_MAP || js_is_js_array(value) ||
           t == LMD_TYPE_FUNC || t == LMD_TYPE_ELEMENT;
}

static inline bool js_coerce_is_bigint(Item value) {
    if (get_type_id(value) != LMD_TYPE_DECIMAL) return false;
    Decimal* dec = (Decimal*)(value.item & 0x00FFFFFFFFFFFFFF);
    return dec && dec->unlimited == DECIMAL_BIGINT;
}

extern "C" Item js_to_primitive(Item value, JsHint hint) {
    TypeId vt = get_type_id(value);
    if (!is_object_value(value)) return value;

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
        Item pv = js_map_shape_lookup_ext(value.map, "__primitiveValue__", 18, &own_pv);
        bool pv_is_symbol = get_type_id(pv) == LMD_TYPE_INT && it2i(pv) <= -(int64_t)JS_SYMBOL_BASE;
        if (own_pv && !js_coerce_is_bigint(pv) && !pv_is_symbol) {
            bool has_vo = false, has_ts = false, has_tp = false;
            js_map_shape_lookup_ext(value.map, "valueOf", 7, &has_vo);
            js_map_shape_lookup_ext(value.map, "toString", 8, &has_ts);
            has_tp = it2b(js_has_own_property(value, k_sym_to_primitive()));
            if (!has_vo && !has_ts && !has_tp) return pv;
        }
    }

    // Step 2: @@toPrimitive lookup (prototype-chain walk via js_get_key_default).
    JS_ASSIGN_OR_RETURN(to_prim, js_get_key_default(value, k_sym_to_primitive()));
    TypeId tp_type = get_type_id(to_prim);
    bool tp_present = (to_prim.item != ItemNull.item &&
                       tp_type != LMD_TYPE_UNDEFINED &&
                       tp_type != LMD_TYPE_NULL);

    if (tp_present && !js_is_callable(to_prim)) {
        // ES §7.1.1 step 2.b.i: callable check.
        return js_throw_type_error("@@toPrimitive is not a function");
    }

    if (tp_present) {
        // Note: heap_create_name(hint_str) interns; cheap and safe to
        // re-allocate per call (same address as previous calls with the
        // same hint string).
        Item hint_item = js_name_item(hint_str);
        Item args[1] = { hint_item };
        JS_ASSIGN_OR_RETURN(result, js_call_function(to_prim, value, args, 1));
        if (is_object_value(result)) {
            return js_throw_type_error("Cannot convert object to primitive value");
        }
        return result;
    }

    if (vt == LMD_TYPE_MAP) {
        bool raw_proto_found = false;
        Item raw_proto = js_map_shape_lookup_ext(value.map, "__proto__", 9, &raw_proto_found);
        Item proto = raw_proto_found ? raw_proto : js_get_prototype(value);
        TypeId proto_type = get_type_id(proto);
        bool null_proto = proto.item == ItemNull.item || proto.item == ITEM_JS_UNDEFINED ||
            proto_type == LMD_TYPE_NULL || proto_type == LMD_TYPE_UNDEFINED;
        Map* object_proto = js_resolve_object_prototype();
        if (raw_proto_found && null_proto && value.map != object_proto &&
            !js_object_uses_default_object_to_primitive(value)) {
            bool has_vo = false, has_ts = false;
            js_map_shape_lookup_ext(value.map, "valueOf", 7, &has_vo);
            js_map_shape_lookup_ext(value.map, "toString", 8, &has_ts);
            if (!has_vo && !has_ts) {
                return js_throw_type_error("Cannot convert object to primitive value");
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
        JS_ASSIGN_OR_RETURN(fn, js_get_key_default(value, method_keys[i]));
        if (fn.item == ItemNull.item || !js_is_callable(fn)) continue;
        JS_ASSIGN_OR_RETURN(result, js_call_function(fn, value, NULL, 0));
        if (!is_object_value(result)) {
            return result;
        }
    }

    // Step 4: every callable method returned an object → TypeError.
    // v28: DOM/CSSOM elements have non-callable toString/valueOf placeholders
    // (returning boolean ITEM_TRUE for feature detection). Fall through to
    // default string conversion instead of throwing.
    if (vt == LMD_TYPE_MAP && value.map &&
        js_object_uses_default_object_to_primitive(value)) {
        return js_name_item("[object Object]");
    }
    return js_throw_type_error("Cannot convert object to primitive value");
}
