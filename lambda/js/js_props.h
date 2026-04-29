// js_props.h — ES2020 property-model abstract operations (Stage A1)
//
// This header declares the canonical abstract operations from ECMA-262
// §7.3 / §10.1, mirroring spec naming. The intent is that every property
// operation in the engine routes through these — eliminating parallel
// dispatch paths and centralizing the deleted-sentinel / IS_ACCESSOR /
// class-method-dispatch invariants.
//
// See vibe/jube/Transpile_Js38_Refactor.md for the migration plan.
//
// Stage A1 status: declarations only. `ToPropertyKey` is implemented today
// as `js_to_property_key` in js_runtime.cpp; the others are stubs to be
// filled in as call sites are migrated. Existing entry points
// (`js_property_get`, `js_property_set`, `js_delete_property`,
// `js_super_property_set`) remain unchanged for now and continue to be
// the public ABI.

#ifndef LAMBDA_JS_PROPS_H
#define LAMBDA_JS_PROPS_H

#include "../lambda-data.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Property keys (Stage A1)
// ---------------------------------------------------------------------------

// ES §7.1.19 ToPropertyKey(argument).
// Coerces any Item to a canonical property key:
//  - Symbol  → internal `__sym_N` interned string
//  - String  → as-is
//  - INT/FLOAT/BOOL/NULL/UNDEFINED → canonical string via ES ToString
// Idempotent. The single canonical entry point — every operation that takes
// a raw `Item key` MUST call this before consulting shape entries, marker
// strings, or the deleted sentinel.
//
// Implemented: lambda/js/js_runtime.cpp.
Item js_to_property_key(Item key);

// True if `key` is a JS Symbol (encoded as a small INT below the symbol
// sentinel). Implemented in js_runtime.cpp.
int64_t js_key_is_symbol_c(Item key);

// ---------------------------------------------------------------------------
// Property descriptor (Stage A2 — to be introduced)
// ---------------------------------------------------------------------------

// PropertyDescriptor flags. The `JS_PD_*` prefix is reserved for the
// future descriptor table; `JSPD_*` (in js_property_attrs.h) is the legacy
// shape-flag scheme that this will replace.
//
// enum JsPdFlags : uint8_t {
//     JS_PD_WRITABLE     = 1u << 0,
//     JS_PD_ENUMERABLE   = 1u << 1,
//     JS_PD_CONFIGURABLE = 1u << 2,
//     JS_PD_ACCESSOR     = 1u << 3,
//     JS_PD_DELETED      = 1u << 4,
//     JS_PD_HAS_VALUE    = 1u << 5,
//     JS_PD_HAS_GET      = 1u << 6,
//     JS_PD_HAS_SET      = 1u << 7,
// };

// ---------------------------------------------------------------------------
// Abstract operations (Stage A1 — to be implemented incrementally)
// ---------------------------------------------------------------------------

// ES §10.1.5 OrdinaryGet (O, P, Receiver)
// Item js_ordinary_get(Item O, Item P, Item Receiver);

// ES §10.1.9 OrdinarySet (O, P, V, Receiver)
// int  js_ordinary_set(Item O, Item P, Item V, Item Receiver);

// ES §10.1.10 OrdinaryDelete (O, P)
// int  js_ordinary_delete(Item O, Item P);

// ES §10.1.7 OrdinaryHasProperty (O, P)
// int  js_ordinary_has_property(Item O, Item P);

// ES §10.1.2 OrdinaryGetOwnProperty (O, P)
// returns descriptor in caller-supplied out-param; false if not present.

// ES §10.1.6 OrdinaryDefineOwnProperty (O, P, Desc)
// int  js_ordinary_define_own_property(Item O, Item P, /* PropertyDescriptor */ const void* desc);

#ifdef __cplusplus
}
#endif

#endif  // LAMBDA_JS_PROPS_H
