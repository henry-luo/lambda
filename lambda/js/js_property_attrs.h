/**
 * JS Property Attribute helpers — Phase 1 of the accessor descriptor refactor.
 *
 * Replaces the legacy `__get_X`/`__set_X`/`__nw_X`/`__ne_X`/`__nc_X` magic-key
 * scheme with first-class metadata carried inline on `ShapeEntry::flags`.
 *
 * Storage scheme (Option 2 — LMD_TYPE_FUNC tagging):
 * - `ShapeEntry::flags` holds the JSPD_* attribute bits (W/E/C/IS_ACCESSOR).
 *   Inverse-bit encoded so 0 == JS default (writable, enumerable, configurable, data).
 * - For data props, the map data slot at `byte_offset` holds the value Item directly.
 * - For accessor props (IS_ACCESSOR set), the slot holds an Item whose pointer
 *   points to a heap-allocated `JsAccessorPair`. The pair starts with
 *   `type_id = LMD_TYPE_FUNC` so `get_type_id()` returns FUNC for tag-safety;
 *   consumers MUST consult the shape flag before invoking it as a function.
 *
 * Phase 1a (this file) provides foundation primitives only. Phase 1b will
 * migrate `js_globals.cpp` ValidateAndApplyPropertyDescriptor and
 * `js_object_get_own_property_descriptor` to use these helpers. Phase 2 will
 * migrate `js_runtime.cpp` property_get/property_set readers.
 */
#pragma once

#include "../lambda.h"
#include "../lambda-data.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// ShapeEntry attribute predicates (inline, header-only)
// =============================================================================

// Inverse-bit-encoded attrs — pool_calloc'd entries auto-default to JS-conformant
// values (writable, enumerable, configurable, data property) without explicit init.

static inline bool jspd_is_writable(const ShapeEntry* se) {
    return !se || !(se->flags & JSPD_NON_WRITABLE);
}
static inline bool jspd_is_enumerable(const ShapeEntry* se) {
    return !se || !(se->flags & JSPD_NON_ENUMERABLE);
}
static inline bool jspd_is_configurable(const ShapeEntry* se) {
    return !se || !(se->flags & JSPD_NON_CONFIGURABLE);
}
static inline bool jspd_is_accessor(const ShapeEntry* se) {
    return se && (se->flags & JSPD_IS_ACCESSOR);
}

// Mutators (must be called only on shapes that are NOT pool-deduplicated, i.e.
// shapes private to a single map instance). For deduplicated shapes the caller
// must first transition the map to a fresh shape.
static inline void jspd_set_writable(ShapeEntry* se, bool w) {
    if (!se) return;
    if (w) se->flags &= (uint8_t)~JSPD_NON_WRITABLE;
    else   se->flags |= JSPD_NON_WRITABLE;
}
static inline void jspd_set_enumerable(ShapeEntry* se, bool e) {
    if (!se) return;
    if (e) se->flags &= (uint8_t)~JSPD_NON_ENUMERABLE;
    else   se->flags |= JSPD_NON_ENUMERABLE;
}
static inline void jspd_set_configurable(ShapeEntry* se, bool c) {
    if (!se) return;
    if (c) se->flags &= (uint8_t)~JSPD_NON_CONFIGURABLE;
    else   se->flags |= JSPD_NON_CONFIGURABLE;
}
static inline void jspd_set_accessor(ShapeEntry* se, bool a) {
    if (!se) return;
    if (a) se->flags |= JSPD_IS_ACCESSOR;
    else   se->flags &= (uint8_t)~JSPD_IS_ACCESSOR;
}

// =============================================================================
// JsAccessorPair allocation and tagging
// =============================================================================

// Allocate a fresh JsAccessorPair on the JS GC heap. type_id is initialized to
// LMD_TYPE_FUNC for tag compatibility with Option 2 storage. Either getter or
// setter may be ItemNull (representing absent get / absent set per ES spec).
JsAccessorPair* js_alloc_accessor_pair(Item getter, Item setter);

// Wrap a JsAccessorPair* as an Item suitable for storing in a map slot.
// The Item's high-byte type_id reads as LMD_TYPE_FUNC (via the pair's first byte
// when type_id() dereferences). Callers MUST set the IS_ACCESSOR flag on the
// owning ShapeEntry before reads can correctly interpret this slot.
static inline Item js_accessor_pair_to_item(JsAccessorPair* p) {
    Item it; it.function = (Function*)p; return it;
}

// Recover a JsAccessorPair* from a slot Item. Caller is responsible for verifying
// `jspd_is_accessor(shape_entry)` first; otherwise behavior is undefined.
static inline JsAccessorPair* js_item_to_accessor_pair(Item it) {
    return (JsAccessorPair*)it.function;
}

// =============================================================================
// ShapeEntry locator
// =============================================================================
//
// Find the ShapeEntry for a property `name` on a JS object. Handles:
//   - LMD_TYPE_MAP:   walks obj.map's TypeMap shape
//   - LMD_TYPE_ARRAY: walks the companion props_map's TypeMap shape (if any)
//   - LMD_TYPE_FUNC:  walks the function's properties_map TypeMap shape (if any)
//
// Returns NULL if no shape entry exists for that name (i.e. the property has not
// been added yet, or the object kind has no map). For Phase 1b dual-write mode,
// callers use this to set/clear flags on the ShapeEntry corresponding to a JS
// property that was just written via the legacy magic-key scheme.
ShapeEntry* js_find_shape_entry(Item obj, const char* name, int name_len);

// Convenience: set flags bits on the ShapeEntry for `name` (no-op if not found).
// `set_mask` bits are OR'd in; `clear_mask` bits are cleared. Apply set first then clear.
void js_shape_entry_update_flags(Item obj, const char* name, int name_len,
                                 uint8_t set_mask, uint8_t clear_mask);

// A2-T3: set or clear the JSPD_IS_ACCESSOR bit on the ShapeEntry for `name`.
// Goes through the same Map-local clone primitive as
// `js_shape_entry_update_flags` so siblings sharing a TypeMap (via per-callsite
// shape cache or constructor pre-shaping) are not affected. No-op if the entry
// does not exist or the bit is already in the requested state.
void js_shape_entry_set_accessor(Item obj, const char* name, int name_len, bool is_accessor);

// =============================================================================
// Stage A3: shape-flag-first attribute query helpers
// =============================================================================
//
// Query whether the named own property of map `m` is enumerable / writable /
// configurable. The shape entry `se` (if non-NULL) is consulted FIRST: if its
// JSPD_NON_* flag bit is set, the property is treated accordingly without
// further probing. Otherwise (or when `se == NULL`), a legacy `__ne_X` /
// `__nw_X` / `__nc_X` marker probe on `m` provides the answer. This combined
// check is robust against shared (pool-deduplicated) shape entries whose flag
// bits could not be safely mutated by `js_dual_write_marker_flags`.
//
// Use these to replace the repetitive `snprintf("__ne_%.*s") + map_get_fast_ext
// + js_is_truthy` pattern across enumerator/spread/assign sites.
bool js_props_query_enumerable(Map* m, ShapeEntry* se,
                                const char* name, int name_len);
bool js_props_query_writable(Map* m, ShapeEntry* se,
                              const char* name, int name_len);
bool js_props_query_configurable(Map* m, ShapeEntry* se,
                                  const char* name, int name_len);

// Item-accepting wrappers — accept any object (MAP / FUNC / ARRAY) and resolve
// the appropriate Map* + ShapeEntry* internally before delegating to the
// Map*-based helpers above. Use these at sites that operate on a generic
// `Item obj` to avoid manual MAP/FUNC dispatch.
bool js_props_obj_query_enumerable(Item obj, const char* name, int name_len);
bool js_props_obj_query_writable(Item obj, const char* name, int name_len);
bool js_props_obj_query_configurable(Item obj, const char* name, int name_len);

// =============================================================================
// Stage A2.6: attribute marker write helpers
// =============================================================================
//
// Centralize the repeated `snprintf("__nw_%.*s") + heap_create_name +
// js_defprop_set_marker(obj, k, b2it(<bool>))` pattern. Each helper writes
// the inverse marker (`__nw_X`/`__ne_X`/`__nc_X`) on `obj`:
//   - `writable=true`  → marker value FALSE  (writable; default)
//   - `writable=false` → marker value TRUE   (non-writable)
//   - same inverse semantics for enumerable / configurable.
//
// All three route through `js_defprop_set_marker`, which handles the
// MAP / FUNC / ARRAY companion-map dispatch and triggers
// `js_dual_write_marker_flags` to keep the JSPD_NON_* shape bit in sync.
//
// Routes ~30 raw snprintf sites in js_runtime.cpp / js_globals.cpp /
// js_props.cpp / js_property_attrs.cpp through one chokepoint, eliminating
// the "one more place forgot to inverse-encode the marker" bug class. Use
// these everywhere a non-* marker is being written or cleared.
//
// Property name length must satisfy 0 < name_len < 240 (defensive bound for
// the 256-byte stack buffer including the 5-byte prefix and NUL).
void js_attr_set_writable(Item obj, const char* name, int name_len, bool writable);
void js_attr_set_enumerable(Item obj, const char* name, int name_len, bool enumerable);
void js_attr_set_configurable(Item obj, const char* name, int name_len, bool configurable);

// =============================================================================
// Phase 2a: Universal dual-write hook
// =============================================================================
//
// Inspect a property write `(obj, key, value)`. If `key` is a marker key
// (`__nw_X` / `__ne_X` / `__nc_X` / `__get_X` / `__set_X`), extract the
// underlying property name X and update the corresponding bit on the X
// ShapeEntry::flags. Truthy `value` sets the bit; tombstoned-or-falsy clears it
// (except for accessor markers, which are sticky once set — Phase 2 readers
// will manage clear-on-tombstone after migration).
//
// Returns true if the key matched a marker pattern (caller may use this hint
// to know that a flag mutation occurred). No-op (returns false) for any
// non-marker key, non-string key, or unknown prefix.
//
// This is installed at the top of `js_property_set` so that ALL writers —
// `js_defprop_set_marker`, transpiler-emitted accessors, builtin prototype
// installers, mark_builder JS construction, etc. — populate flags without
// requiring per-callsite changes.
bool js_dual_write_marker_flags(Item obj, Item key, Item value);

// =============================================================================
// Phase 2b: Reader fast-path helpers
// =============================================================================
//
// `js_prop_attrs_fast_path` returns a tri-state hint about whether the legacy
// magic-key probe for an attribute marker can be skipped:
//   1  → shape entry exists AND attribute is the JS default (e.g. writable):
//        the marker is provably absent on this map; SKIP the probe entirely.
//   0  → shape entry exists AND attribute is non-default (e.g. non-writable):
//        the marker is provably present; act as if probe found a truthy value.
//  -1  → shape entry not found (property hasn't been added yet, or this object
//        kind has no map): fall through to the legacy probe — required for
//        edge cases like ro_globals which set `__nw_X` markers on globalThis
//        for properties that live on the prototype chain.
//
// `attr_flag` must be exactly one of JSPD_NON_WRITABLE / JSPD_NON_ENUMERABLE /
// JSPD_NON_CONFIGURABLE / JSPD_IS_ACCESSOR.
//
// IMPORTANT: do not use this for accessor (get/set) probes yet — Phase 2a
// flag-population for accessor-only properties is incomplete (no shape entry
// is created for X when only `__get_X` is stored). Use only for nw/ne/nc.
static inline int js_prop_attrs_fast_path(Item obj, const char* name, int name_len,
                                          uint8_t attr_flag) {
    ShapeEntry* se = js_find_shape_entry(obj, name, name_len);
    if (!se) return -1;
    return (se->flags & attr_flag) ? 0 : 1;
}

// =============================================================================
// Phase 3+4 Stage A: unified accessor producer
// =============================================================================
//
// `js_install_native_accessor` is the single chokepoint for installing native
// (C/builtin) accessor properties — RegExp prototype getters, length getter,
// Symbol.species, Symbol.iterator, TypedArray buffer/byteLength/byteOffset, etc.
// During Stage A it preserves the legacy `__get_X`/`__set_X` magic-key storage
// (via js_property_set, which routes through dual-write to populate shape
// flags). Stage B will switch the underlying storage to put the
// `JsAccessorPair` Item in the slot for X with `JSPD_IS_ACCESSOR` set on the
// shape entry, with a single-line edit here.
//
// `attrs` uses the JSPD_* inverse-bit encoding: pass 0 for ES default
// (enumerable + configurable). For ES accessor defaults (non-enumerable,
// configurable — what RegExp/length/etc. use), pass `JSPD_NON_ENUMERABLE`.
//
// `getter` and `setter` may each be ItemNull (representing absent get/set).
void js_install_native_accessor(Item obj, Item name, Item getter, Item setter,
                                uint8_t attrs);

// =============================================================================
// Phase 4: transpiler accessor producer (partial / merging)
// =============================================================================
//
// `js_define_accessor_partial` is the chokepoint for installing user-defined
// accessors emitted by the transpiler (class methods, soon object literals).
// Unlike `js_install_native_accessor` (which receives both halves at once),
// this helper handles the case where getter and setter for the same property
// are emitted by separate AST traversals: it locates any existing
// `JsAccessorPair` under name X and merges in the new half, or allocates a
// fresh pair if none exists.
//
// `is_setter`: 0 → install as getter; 1 → install as setter.
// `attrs`: JSPD_* bits (e.g. JSPD_NON_ENUMERABLE for class methods; pass 0 for
// object literal accessors which should default to enumerable+configurable).
// IS_ACCESSOR is always set on the resulting shape entry.
void js_define_accessor_partial(Item obj, Item name, Item fn, int is_setter,
                                uint8_t attrs);

// Phase-5C transpiler chokepoint: 4-arg wrapper around
// `js_define_accessor_partial(..., attrs=0)` returning the object Item so MIR
// emit sites can replace the legacy `js_make_getter_key`+`js_property_set`
// pair with a single call. `is_setter` is an int (0/1) for MIR ABI simplicity.
Item js_install_user_accessor(Item obj, Item name, Item fn, int is_setter);

// =============================================================================
// Phase 4: js_property_set intercept for legacy __get_X/__set_X writes
// =============================================================================
//
// `js_intercept_accessor_marker` is called at the top of `js_property_set`.
// If `key` matches the `__get_X`/`__set_X` magic-key pattern (transpiler
// fallback, computed accessor literals, or any other legacy emitter), it
// extracts X and routes the write through `js_define_accessor_partial`,
// merging the half into the existing pair (or allocating a fresh one).
//
// Returns true if intercepted (caller MUST skip the normal store path).
// Returns false for any non-marker key.
bool js_intercept_accessor_marker(Item obj, Item key, Item value);

// Walk own + prototype chain for an accessor pair on `name`. Returns the pair
// pointer (without invoking getter/setter) for the first shape entry along
// the chain that has JSPD_IS_ACCESSOR set. Used by setter dispatch in
// `js_property_set` to discover inherited accessors after Phase 4 removed
// the `__set_X` legacy keys that the old proto-walk relied on.
JsAccessorPair* js_find_accessor_pair_inheritable(Item obj, const char* name,
                                                  int name_len);

#ifdef __cplusplus
}
#endif
