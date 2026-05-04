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
static inline Item k_primitive_value(void) {
    return (Item){.item = s2it(heap_create_name("__primitiveValue__", 18))};
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
        if (own_pv) {
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
        (value.map->map_kind == MAP_KIND_DOM ||
         value.map->map_kind == MAP_KIND_CSSOM ||
         value.map->map_kind == MAP_KIND_DOC_PROXY ||
         value.map->map_kind == MAP_KIND_FOREIGN_DOC)) {
        return (Item){.item = s2it(heap_create_name("[object Object]"))};
    }
    js_throw_type_error("Cannot convert object to primitive value");
    return ItemNull;
}
