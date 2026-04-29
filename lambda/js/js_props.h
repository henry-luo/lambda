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

// Outcome of `js_ordinary_get_own` (own-property lookup with IS_ACCESSOR
// dispatch). The kernel of the `[[Get]]` algorithm — extracted here so it
// can be shared by all property-read paths and tested directly in the
// Stage B harness.
typedef enum {
    JS_OWN_NOT_FOUND = 0,  // no own slot; caller should walk prototype chain
    JS_OWN_DELETED   = 1,  // own slot held the deleted-sentinel; caller should
                           // walk prototype chain but record the deletion
                           // (Object.prototype top-of-chain semantics)
    JS_OWN_READY     = 2,  // *out_value is the resolved [[Get]] result
                           // (data value, getter return, setter-only undefined,
                           //  or a thrown private-field error). Caller MUST
                           // return *out_value verbatim and NOT walk further.
} JsOwnGetStatus;

// Look up own property `key` on `object` (must be LMD_TYPE_MAP). If the slot
// carries IS_ACCESSOR, dispatch the getter using `Receiver` as `this` (or
// fall back to `object` when `Receiver.item == 0`). Does NOT walk the
// prototype chain.
//
// `key` must already be a canonical property key (string or interned symbol
// key); call `js_to_property_key` first if the caller has a raw Item.
//
// On JS_OWN_READY, *out_value is set. On JS_OWN_NOT_FOUND / JS_OWN_DELETED,
// *out_value is unspecified.
JsOwnGetStatus js_ordinary_get_own(Item object, Item key, Item Receiver,
                                    Item* out_value);

// ES §10.1.5 OrdinaryGet (O, P, Receiver) — Stage A1: not yet implemented.
// Item js_ordinary_get(Item O, Item P, Item Receiver);

// Outcome of `js_ordinary_set_via_accessor` (Stage A1.4) — the inherited-
// accessor-setter dispatch kernel.
typedef enum {
    JS_SET_NOT_FOUND   = 0,  // no IS_ACCESSOR pair found on the chain; caller
                             // should proceed with the normal data write path
    JS_SET_DISPATCHED  = 1,  // pair->setter was called with Receiver as `this`;
                             // caller MUST return value verbatim
    JS_SET_NO_SETTER   = 2,  // pair found but pair->setter == ItemNull;
                             // caller decides strict-throw vs sloppy no-op
} JsSetterDispatchStatus;

// Walk own + proto chain for an IS_ACCESSOR pair under (`name`, `name_len`).
// If found and the pair has a callable setter, dispatch with `Receiver` as
// `this` (or `object` if Receiver.item == 0) and return JS_SET_DISPATCHED.
// If found without a setter, return JS_SET_NO_SETTER. Otherwise return
// JS_SET_NOT_FOUND.
//
// This is the centralized kernel for the inherited-accessor branch of
// `js_property_set` and `js_super_property_set`. It replaces inline
// `js_find_accessor_pair_inheritable` + dispatch sequences.
JsSetterDispatchStatus js_ordinary_set_via_accessor(Item object,
                                                     const char* name,
                                                     int name_len,
                                                     Item value,
                                                     Item Receiver);

// Outcome of `js_ordinary_get_own_descriptor` (Stage A1.5) — read-only
// inspector for own slots. Does NOT dispatch the getter; reports the
// descriptor kind so callers can branch on accessor-vs-data without
// touching shape-entry / slot internals.
typedef enum {
    JS_DESC_NONE     = 0,  // no own slot under this name
    JS_DESC_DELETED  = 1,  // own slot held the deleted sentinel
    JS_DESC_DATA     = 2,  // own data slot; *out_value set, *out_pair NULL
    JS_DESC_ACCESSOR = 3,  // own IS_ACCESSOR slot; *out_pair set, *out_value
                           // unspecified. Either getter or setter (or both)
                           // may be ItemNull — caller checks.
} JsOwnDescKind;

// Inspect own property descriptor for (`object`, `name`, `name_len`).
// `object` must be LMD_TYPE_MAP. Returns descriptor kind and populates
// the relevant out-param. Either out-param may be NULL if the caller does
// not need that information for that descriptor kind.
//
// This is the read-only counterpart to `js_ordinary_get_own`: callers that
// need to *detect* a setter-only / getter-only accessor (e.g. splice
// length-getter check, object-rest setter-only shadowing) use this, while
// callers that need to *resolve* a property value use `js_ordinary_get_own`.
JsOwnDescKind js_ordinary_get_own_descriptor(Item object,
                                              const char* name,
                                              int name_len,
                                              JsAccessorPair** out_pair,
                                              Item* out_value);

// Outcome of `js_ordinary_resolve_shape_value` (Stage A1.7) — the
// shape-iteration value-resolution kernel.
typedef enum {
    JS_RESOLVE_DELETED = 0,  // slot held the deleted sentinel; caller should
                             // skip this entry
    JS_RESOLVE_VALUE   = 1,  // *out_value populated (data slot or getter
                             // return); caller proceeds normally
    JS_RESOLVE_THREW   = 2,  // accessor getter threw; caller MUST propagate
                             // by checking js_check_exception() and bailing
} JsResolveFieldStatus;

// Resolve the value carried by shape-entry `e` belonging to map `m`. If the
// entry is IS_ACCESSOR, dispatches the getter using `receiver` as `this`.
// Returns JS_RESOLVE_DELETED for sentinel slots, JS_RESOLVE_VALUE on success
// (with *out_value set), or JS_RESOLVE_THREW if the getter raised.
//
// This is the kernel used by shape-iteration loops (object spread,
// Object.assign, CopyDataProperties) — replaces inline `_map_read_field`
// + sentinel-check + IS_ACCESSOR + pair-cast + getter-dispatch sequences.
//
// Setter-only accessors (getter == ItemNull) resolve to JS undefined per
// ES §CopyDataProperties and §Object.assign.
JsResolveFieldStatus js_ordinary_resolve_shape_value(ShapeEntry* e,
                                                      Map* m,
                                                      Item receiver,
                                                      Item* out_value);

// ---------------------------------------------------------------------------
// PropertyDescriptor (Stage A2 — read-side facade)
// ---------------------------------------------------------------------------
//
// Unified property-descriptor record. ES §6.2.5 defines six fields; we
// represent them with a flags byte + value/getter/setter. The `present`
// bits indicate which fields are populated (mirrors ES IsAccessorDescriptor
// / IsDataDescriptor / IsGenericDescriptor logic).
//
// Stage A2.1 INTRODUCES the inspector kernel that synthesizes a descriptor
// from current storage (shape flags + slot + legacy `__nc_`/`__ne_`/`__nw_`
// markers + `__get_`/`__set_` markers). Storage layout is unchanged; this
// is a read-side consolidation.
//
// Stage A2.2+ will introduce the write-side kernel, route all
// Object.defineProperty / accessor-install paths through it, and finally
// collapse storage to a dense PropertyDescriptor table — eliminating the
// JSPD_IS_ACCESSOR + JS_DELETED_SENTINEL_VAL + legacy-marker triplets.

#define JS_PD_HAS_VALUE        0x01u  // descriptor carries [[Value]]
#define JS_PD_HAS_GET          0x02u  // descriptor carries [[Get]]
#define JS_PD_HAS_SET          0x04u  // descriptor carries [[Set]]
#define JS_PD_HAS_WRITABLE     0x08u  // descriptor carries [[Writable]]
#define JS_PD_HAS_ENUMERABLE   0x10u  // descriptor carries [[Enumerable]]
#define JS_PD_HAS_CONFIGURABLE 0x20u  // descriptor carries [[Configurable]]

#define JS_PD_WRITABLE         0x40u  // [[Writable]] bit (when HAS_WRITABLE set)
#define JS_PD_ENUMERABLE       0x80u  // [[Enumerable]] bit (when HAS_ENUMERABLE)
// [[Configurable]] uses bit in `flags2` to keep this a single byte. Use
// helper functions below.

typedef struct JsPropertyDescriptor {
    uint8_t flags;     // JS_PD_HAS_* + JS_PD_WRITABLE / JS_PD_ENUMERABLE
    uint8_t flags2;    // bit0: configurable; bits1-7 reserved
    uint8_t reserved[6];
    Item value;        // [[Value]] — valid iff JS_PD_HAS_VALUE
    Item getter;       // [[Get]]   — valid iff JS_PD_HAS_GET
    Item setter;       // [[Set]]   — valid iff JS_PD_HAS_SET
} JsPropertyDescriptor;

static inline bool js_pd_is_accessor(const JsPropertyDescriptor* d) {
    return (d->flags & (JS_PD_HAS_GET | JS_PD_HAS_SET)) != 0;
}
static inline bool js_pd_is_data(const JsPropertyDescriptor* d) {
    return (d->flags & (JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE)) != 0;
}
static inline bool js_pd_is_configurable(const JsPropertyDescriptor* d) {
    return (d->flags2 & 0x01u) != 0;
}
static inline void js_pd_set_configurable(JsPropertyDescriptor* d, bool b) {
    if (b) d->flags2 |= 0x01u; else d->flags2 &= (uint8_t)~0x01u;
}

// Synthesize the full property descriptor for own property (`name`,
// `name_len`) on `object`. `object` must be LMD_TYPE_MAP or LMD_TYPE_FUNC.
//
// Returns true if an own property is present (descriptor populated).
// Returns false if no own property exists (or slot held the deleted
// sentinel). On false, *out is left in an unspecified state.
//
// This is the unified read kernel. It folds together:
//   - shape flag inspection (JSPD_IS_ACCESSOR / writable / enum / config),
//   - slot read with deleted-sentinel awareness,
//   - JsAccessorPair extraction for accessor descriptors,
//   - legacy `__get_X` / `__set_X` marker fallback for native bindings
//     that have not yet been migrated to the IS_ACCESSOR scheme,
//   - legacy `__nc_X` / `__ne_X` / `__nw_X` flag-marker fallback.
//
// All Object.getOwnPropertyDescriptor / Reflect.getOwnPropertyDescriptor /
// Object.{is,seal,freeze} / Object.assign read-side paths should route
// through this — collapsing the 5+ parallel descriptor-synthesis sites.
bool js_get_own_property_descriptor(Item object,
                                     const char* name,
                                     int name_len,
                                     JsPropertyDescriptor* out);

// ---------------------------------------------------------------------------
// PropertyDescriptor (Stage A2.3 — write-side kernel)
// ---------------------------------------------------------------------------
//
// ES §6.2.5.5 ToPropertyDescriptor — coerce a JS descriptor object into a
// canonical JsPropertyDescriptor record. Performs the standard validation:
//   - rejects non-object descriptors (TypeError)
//   - rejects mixed accessor + data descriptors (TypeError)
//   - rejects non-callable getter / setter (TypeError)
// On any TypeError, throws via `js_throw_value` and returns false; *out is
// left in an unspecified state. On success, returns true with *out
// populated; HAS_* bits indicate which fields the JS object carried.
//
// HAS_WRITABLE / HAS_ENUMERABLE / HAS_CONFIGURABLE are set whenever the key
// is present in the descriptor object, regardless of value (consistent with
// `HasProperty(Desc, "writable")` etc. in the spec). The corresponding
// boolean payload (JS_PD_WRITABLE, JS_PD_ENUMERABLE, configurable bit in
// flags2) reflects the coerced value.
//
// HAS_GET / HAS_SET are set whenever the key is present, even if the value
// is `undefined` — matching ES2020 ToPropertyDescriptor semantics where an
// explicit `{get: undefined}` makes the descriptor an accessor descriptor
// with no getter.
bool js_descriptor_from_object(Item desc_obj, JsPropertyDescriptor* out);

// ES §10.1.6.3 ValidateAndApplyPropertyDescriptor (apply-only).
//
// Apply descriptor `pd` to own property (`name`, `name_len`) on `object`.
// `object` must be LMD_TYPE_MAP, LMD_TYPE_ARRAY, LMD_TYPE_FUNC, or
// LMD_TYPE_ELEMENT. The kernel performs storage writes only — the caller
// is responsible for validation (mixed accessor/data, non-configurable
// invariants, etc.). `is_new_property` controls whether absent attribute
// fields default to "non-X" (true: set non-* markers for new properties)
// or are left untouched (false: existing markers preserved per ES spec).
//
// Behavior summary:
//   - HAS_GET | HAS_SET → routes through `js_define_accessor_partial`
//     (allocates / merges JsAccessorPair, sets IS_ACCESSOR shape flag).
//   - HAS_VALUE → writes data slot via js_property_set / js_func_init_property,
//     clears IS_ACCESSOR if the slot was previously an accessor (was_accessor),
//     and removes legacy __get_/__set_ markers.
//   - HAS_WRITABLE / HAS_ENUMERABLE / HAS_CONFIGURABLE → write the
//     corresponding `__nw_X` / `__ne_X` / `__nc_X` markers (set when the
//     attribute is FALSE, clear when TRUE). For new properties, absent
//     attributes default to non-* (writable=false, enumerable=false,
//     configurable=false per ES2020 default descriptor).
//
// This is the centralized write kernel for Object.defineProperty,
// Object.defineProperties, Reflect.defineProperty, and the accessor-install
// paths. Stage A2.4 will route ValidateAndApplyPropertyDescriptor's storage
// tail through this; storage layout (legacy markers + JsAccessorPair slots)
// is unchanged so unmigrated readers continue to work.
void js_define_own_property_from_descriptor(Item object,
                                             const char* name,
                                             int name_len,
                                             const JsPropertyDescriptor* pd,
                                             bool is_new_property);

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
