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

#ifdef __cplusplus
}
#endif
