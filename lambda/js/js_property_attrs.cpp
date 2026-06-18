/**
 * Phase 1a foundation: JS property attribute and accessor primitives.
 * See js_property_attrs.h for design notes.
 */

#include "js_property_attrs.h"
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_state_guards.h"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include <string.h>
#include <stdio.h>

extern "C" void* heap_calloc(size_t size, TypeId type);
String* heap_create_name(const char* name, size_t len);
extern "C" Item js_object_is(Item left, Item right);
extern void map_put(Map* mp, String* key, Item value, Input* input);

// Mirror of JsFuncProps from js_globals.cpp / js_runtime.cpp — only used here to
// reach `properties_map`. Layout MUST stay in sync with the canonical definitions.
struct JsFuncPropsView {
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

extern "C" JsAccessorPair* js_alloc_accessor_pair(Item getter, Item setter) {
    // Allocate as LMD_TYPE_FUNC so the GC tracer (if any) treats it like a Function
    // and so `Item.type_id()` returns FUNC for tag-safety. Callers must rely on
    // ShapeEntry::flags JSPD_IS_ACCESSOR to disambiguate accessor pair from real
    // Function before invoking it.
    JsAccessorPair* p = (JsAccessorPair*)heap_calloc(sizeof(JsAccessorPair), LMD_TYPE_FUNC);
    if (!p) return nullptr;
    p->type_id = LMD_TYPE_FUNC;
    p->getter = getter;
    p->setter = setter;
    return p;
}

// Locate the underlying TypeMap holding shape entries for a JS object.
// Arrays use a companion Map stored in arr->extra (NULL if not yet allocated).
// P0 safety: typical userspace pointers on macOS / Linux fit in 48 bits.
// Anything above that is a tagged Item value that somehow ended up in the
// `type` slot of a Map struct (lib_marked.js triggers this — a callback
// receives an object whose Map.type field reads as 0x1a00000000000000).
// Returning nullptr makes the prototype-chain walks short-circuit cleanly
// instead of dereferencing the garbage TypeMap pointer.
static inline bool js_is_plausible_typemap_ptr(void* p) {
    return p && (uintptr_t)p <= 0x0000FFFFFFFFFFFFULL;
}

static TypeMap* js_obj_typemap(Item obj) {
    TypeId t = get_type_id(obj);
    if (t == LMD_TYPE_MAP) {
        Map* m = obj.map;
        if (!m) return nullptr;
        TypeMap* tm = (TypeMap*)m->type;
        return js_is_plausible_typemap_ptr(tm) ? tm : nullptr;
    }
    if (t == LMD_TYPE_ARRAY) {
        Array* arr = obj.array;
        if (!arr || arr->extra == 0) return nullptr;
        Map* m = (Map*)(uintptr_t)arr->extra;
        if (!m) return nullptr;
        TypeMap* tm = (TypeMap*)m->type;
        return js_is_plausible_typemap_ptr(tm) ? tm : nullptr;
    }
    if (t == LMD_TYPE_FUNC) {
        JsFuncPropsView* fn = (JsFuncPropsView*)obj.function;
        if (!fn || fn->properties_map.item == 0) return nullptr;
        if (get_type_id(fn->properties_map) != LMD_TYPE_MAP) return nullptr;
        Map* m = fn->properties_map.map;
        if (!m) return nullptr;
        TypeMap* tm = (TypeMap*)m->type;
        return js_is_plausible_typemap_ptr(tm) ? tm : nullptr;
    }
    return nullptr;
}

extern "C" ShapeEntry* js_find_shape_entry(Item obj, const char* name, int name_len) {
    TypeMap* tm = js_obj_typemap(obj);
    if (!tm) return nullptr;
    // Fast path via hash table when populated.
    ShapeEntry* hit = typemap_hash_lookup(tm, name, name_len);
    if (hit) return hit;
    // Fallback linear scan (in case hash table overflowed). Match
    // js_map_get_fast's last-writer-wins behavior for duplicate shape names.
    ShapeEntry* found = nullptr;
    for (ShapeEntry* e = tm->shape; e; e = e->next) {
        if (e->name && (int)e->name->length == name_len &&
            memcmp(e->name->str, name, name_len) == 0) {
            found = e;
        }
    }
    return found;
}

// Locate the underlying Map* whose `type` field would receive a cloned TypeMap
// when an attribute mutation is applied to `obj`. Mirrors js_obj_typemap()'s
// per-TypeId dispatch but returns the Map (not its TypeMap), so the caller can
// rewire `m->type` after cloning. Forward-declared; defined below alongside the
// accessor write path that already uses the same helper.
static Map* js_obj_underlying_map(Item obj);

// A2-T1: clone the underlying TypeMap + ShapeEntry chain for `obj` so that any
// subsequent ShapeEntry mutation (flags, accessor flip, future delete bit) does
// not affect sibling Maps that share the original TypeMap via per-call-site
// shape cache (transpile_js_mir.cpp §7) or constructor pre-shaping.
//
// Idempotent: if the current TypeMap is already this Map's private clone
// (is_private_clone == true), returns it unchanged. Returns the (possibly new)
// TypeMap pointer; returns nullptr if cloning is not possible (no underlying
// map, no js_input pool) — caller decides whether to fall back to in-place
// mutation or skip.
//
// Cloned ShapeEntry's share immutable name StrView*'s with the source (the
// embedded StrView lives at end-of-entry on entries created via
// shape_pool/create_shape_chain or alloc_type-based paths and is itself
// immutable; entries created with separate StrView allocations carry an external
// pointer that is also immutable). Sharing is safe because attribute mutation
// only touches `flags`, never `name`.
static TypeMap* js_typemap_clone_for_mutation(Item obj) {
    Map* underlying = js_obj_underlying_map(obj);
    if (!underlying) return nullptr;
    TypeMap* tm = (TypeMap*)underlying->type;
    if (!tm) return nullptr;
    if (tm->is_private_clone) return tm;
    if (!js_input || !js_input->pool) return nullptr;
    Pool* pool = js_input->pool;

    TypeMap* clone = (TypeMap*)alloc_type(pool, LMD_TYPE_MAP, sizeof(TypeMap));
    if (!clone) return nullptr;
    clone->length = tm->length;
    clone->byte_size = tm->byte_size;
    clone->type_index = tm->type_index;
    clone->has_named_shape = tm->has_named_shape;
    clone->struct_name = tm->struct_name;
    clone->is_private_clone = true;
    // A3-T1: propagate JsClass tag onto the private clone so attribute
    // mutations after a class stamp don't lose class identity.
    clone->js_class = tm->js_class;

    // Clone the shape chain: per-entry shallow copy with `next` rewired and
    // `name`/`type`/`ns`/`default_value` shared with the source (all
    // immutable post-shape-creation). `flags` is copied so the clone starts
    // identical to the source; the caller mutates after this returns.
    ShapeEntry* prev_clone = nullptr;
    ShapeEntry* first_clone = nullptr;
    ShapeEntry* last_clone = nullptr;
    for (ShapeEntry* src = tm->shape; src; src = src->next) {
        ShapeEntry* dst = (ShapeEntry*)pool_calloc(pool, sizeof(ShapeEntry));
        if (!dst) return nullptr;
        dst->name = src->name;
        dst->type = src->type;
        dst->byte_offset = src->byte_offset;
        dst->next = nullptr;
        dst->ns = src->ns;
        dst->default_value = src->default_value;
        dst->flags = src->flags;
        if (!first_clone) first_clone = dst;
        if (prev_clone) prev_clone->next = dst;
        prev_clone = dst;
        last_clone = dst;
    }
    clone->shape = first_clone;
    clone->last = last_clone;

    // Repopulate the per-TypeMap field_index hash (it's a fixed-size open
    // address table on the TypeMap struct itself, so the source's table is
    // not aliased — must rebuild against the cloned entries).
    for (ShapeEntry* e = first_clone; e; e = e->next) {
        typemap_hash_insert(clone, e);
    }

    // Mirror slot_entries if the source published a slot-indexed lookup
    // (constructor-shaped objects go through this path).
    if (tm->slot_entries && tm->slot_count > 0) {
        ShapeEntry** entries = (ShapeEntry**)pool_calloc(pool, tm->slot_count * sizeof(ShapeEntry*));
        if (entries) {
            ShapeEntry* e = first_clone;
            for (int i = 0; i < tm->slot_count && e; i++, e = e->next) {
                entries[i] = e;
            }
            clone->slot_entries = entries;
            clone->slot_count = tm->slot_count;
        }
    }

    underlying->type = clone;
    log_debug("A2-T1: cloned TypeMap %p -> %p for Map %p (%lld fields, slot_count=%d)",
              (void*)tm, (void*)clone, (void*)underlying,
              (long long)tm->length, tm->slot_count);
    return clone;
}

extern "C" void js_shape_entry_update_flags(Item obj, const char* name, int name_len,
                                            uint8_t set_mask, uint8_t clear_mask) {
    if (set_mask == 0 && clear_mask == 0) return;
    // Probe first: if the entry doesn't exist or the mutation is a no-op,
    // skip cloning entirely. This avoids replacing m->type with a fresh
    // TypeMap on Maps whose original type was &EmptyMap (or otherwise
    // length==0) — which would later strand map_put because it only
    // initializes mp->data/data_cap when `!mp->type` and our non-null clone
    // bypasses that init path.
    ShapeEntry* se = js_find_shape_entry(obj, name, name_len);
    if (!se) return;
    uint8_t new_flags = (uint8_t)((se->flags | set_mask) & ~clear_mask);
    if (new_flags == se->flags) return;
    // A2-T2: detach this Map's TypeMap from any siblings before mutating
    // ShapeEntry::flags. If cloning isn't possible (no js_input pool, or no
    // mutable underlying Map), fall back to in-place mutation — preserves
    // pre-clone behavior on edge paths.
    if (js_typemap_clone_for_mutation(obj)) {
        se = js_find_shape_entry(obj, name, name_len);
        if (!se) return;
    }
    se->flags = new_flags;
}

// Public wrapper around the file-static js_typemap_clone_for_mutation. Exposed
// so js_class.h's inline writer (js_class_stamp) can clone the TypeMap before
// stamping the JsClass byte without pulling in this whole TU's surface.
extern "C" TypeMap* js_typemap_clone_for_mutation_pub(Item obj) {
    return js_typemap_clone_for_mutation(obj);
}

extern "C" void js_shape_entry_set_accessor(Item obj, const char* name, int name_len,
                                            bool is_accessor) {
    // Same probe-first / clone / mutate pattern as js_shape_entry_update_flags,
    // restricted to the JSPD_IS_ACCESSOR bit. This is the safe replacement for
    // direct `jspd_set_accessor(se, ...)` calls at sites that hold the (obj,
    // name, name_len) context — the per-Map clone is what lets the in-place
    // mutation be safe even when the TypeMap is shared via shape cache.
    if (is_accessor) {
        js_shape_entry_update_flags(obj, name, name_len, JSPD_IS_ACCESSOR, 0);
    } else {
        js_shape_entry_update_flags(obj, name, name_len, 0, JSPD_IS_ACCESSOR);
    }
}

// A2-T8 tombstone bit mutator. Same per-Map clone safety as the accessor
// helper. Bit-only — does NOT write the legacy JS_DELETED_SENTINEL_VAL slot
// value (callers in the transition phase should call this *and* the existing
// sentinel write site; once readers migrate to the bit, the sentinel write
// can drop). When the property has no shape entry yet (e.g. companion-map
// indexed positions on arrays) this is a no-op — those sites continue to use
// the slot sentinel until AT-4 lands.
extern "C" void js_shape_entry_set_deleted(Item obj, const char* name, int name_len,
                                           bool is_deleted) {
    if (is_deleted) {
        js_shape_entry_update_flags(obj, name, name_len, JSPD_DELETED, 0);
    } else {
        js_shape_entry_update_flags(obj, name, name_len, 0, JSPD_DELETED);
    }
}

// =============================================================================
// Stage A3: shape-flag-first attribute query helpers
// =============================================================================
//
// Prefer ShapeEntry::flags for every ordinary property. Array indices and the
// virtual array `length` are materialized in the companion map before flags are
// changed, so marker fallback is no longer needed for attribute queries.
static inline bool js_attrs_name_is_digits(const char* name, int name_len) {
    if (!name || name_len <= 0 || name_len > 10) return false;
    // Reject leading-zero numerics (per ES CanonicalNumericIndexString).
    if (name_len > 1 && name[0] == '0') return false;
    for (int i = 0; i < name_len; i++) {
        if (name[i] < '0' || name[i] > '9') return false;
    }
    return true;
}

static inline bool js_attrs_name_is_length(const char* name, int name_len) {
    return name && name_len == 6 && memcmp(name, "length", 6) == 0;
}

static int64_t js_attrs_parse_index_name(const char* name, int name_len) {
    if (!js_attrs_name_is_digits(name, name_len)) return -1;
    int64_t index = 0;
    for (int i = 0; i < name_len; i++) {
        index = index * 10 + (int64_t)(name[i] - '0');
    }
    if (index > 0xFFFFFFFELL) return -1;
    return index;
}

static Map* js_attr_ensure_array_props_map(Array* arr) {
    if (!arr) return nullptr;
    if (arr->extra == 0) {
        Item obj = js_new_object();
        obj.map->map_kind = MAP_KIND_ARRAY_PROPS;
        arr->extra = (int64_t)(uintptr_t)obj.map;
    }
    return (Map*)(uintptr_t)arr->extra;
}

static bool js_attr_ensure_array_shape_entry(Item obj, const char* name, int name_len) {
    if (!name || name_len <= 0 || name_len >= 240) return false;
    if (!js_attrs_name_is_digits(name, name_len) &&
        !js_attrs_name_is_length(name, name_len)) return false;

    Item target = ItemNull;
    Array* arr = nullptr;
    TypeId type = get_type_id(obj);
    if (type == LMD_TYPE_ARRAY) {
        arr = obj.array;
        Map* pm = js_attr_ensure_array_props_map(arr);
        if (!pm) return false;
        target = (Item){.map = pm};
    } else if (type == LMD_TYPE_MAP && obj.map &&
               obj.map->map_kind == MAP_KIND_ARRAY_PROPS) {
        target = obj;
    } else {
        return false;
    }

    if (js_find_shape_entry(target, name, name_len)) return true;

    Item name_item = (Item){.item = s2it(heap_create_name(name, (size_t)name_len))};
    Item slot_value = (Item){.item = ITEM_JS_UNDEFINED};
    bool slot_found = false;
    if (get_type_id(target) == LMD_TYPE_MAP) {
        slot_value = js_map_get_fast_ext(target.map, name, name_len, &slot_found);
        if (slot_found && slot_value.item == JS_DELETED_SENTINEL_VAL) slot_found = false;
    }
    if (!slot_found && arr && js_attrs_name_is_digits(name, name_len)) {
        int64_t idx = js_attrs_parse_index_name(name, name_len);
        if (idx >= 0 && idx < arr->length && idx < arr->capacity &&
            arr->items[idx].item != JS_DELETED_SENTINEL_VAL) {
            slot_value = arr->items[idx];
            slot_found = true;
        }
    }

    if (get_type_id(target) == LMD_TYPE_MAP && js_input) {
        map_put(target.map, it2s(name_item), slot_value, js_input);
    } else {
        js_property_set(target, name_item, slot_value);
    }
    if (!js_find_shape_entry(target, name, name_len)) return false;
    if (arr && js_attrs_name_is_digits(name, name_len)) {
        int64_t idx = js_attrs_parse_index_name(name, name_len);
        if (idx >= 0 && idx < arr->length && idx < arr->capacity) {
            arr->items[idx] = (Item){.item = JS_DELETED_SENTINEL_VAL};
        }
    }
    return true;
}

extern "C" bool js_props_query_enumerable(Map* m, ShapeEntry* se,
                                          const char* name, int name_len) {
    (void)m; (void)name; (void)name_len;
    if (se && !jspd_is_enumerable(se)) return false;
    return true;
}

extern "C" bool js_props_query_writable(Map* m, ShapeEntry* se,
                                        const char* name, int name_len) {
    (void)m; (void)name; (void)name_len;
    if (se && !jspd_is_writable(se)) return false;
    return true;
}

extern "C" bool js_props_query_configurable(Map* m, ShapeEntry* se,
                                            const char* name, int name_len) {
    (void)m; (void)name; (void)name_len;
    if (se && !jspd_is_configurable(se)) return false;
    return true;
}

// Resolve the underlying Map* for an object: MAP → obj.map; FUNC →
// fn->properties_map.map (when initialized); ARRAY → companion map (in
// arr->extra). Returns nullptr if the object has no map storage.
static Map* js_obj_resolve_map(Item obj) {
    TypeId t = get_type_id(obj);
    if (t == LMD_TYPE_MAP) return obj.map;
    if (t == LMD_TYPE_FUNC) {
        JsFuncPropsView* fn = (JsFuncPropsView*)obj.function;
        if (!fn || fn->properties_map.item == 0) return nullptr;
        if (get_type_id(fn->properties_map) != LMD_TYPE_MAP) return nullptr;
        return fn->properties_map.map;
    }
    if (t == LMD_TYPE_ARRAY) {
        Array* arr = obj.array;
        if (!arr || arr->extra == 0) return nullptr;
        return (Map*)(uintptr_t)arr->extra;
    }
    return nullptr;
}

extern "C" bool js_props_obj_query_enumerable(Item obj, const char* name, int name_len) {
    if (get_type_id(obj) == LMD_TYPE_ARRAY && name_len == 6 &&
        strncmp(name, "length", 6) == 0) {
        return false;
    }
    ShapeEntry* se = js_find_shape_entry(obj, name, name_len);
    Map* m = js_obj_resolve_map(obj);
    return js_props_query_enumerable(m, se, name, name_len);
}

extern "C" bool js_props_obj_query_writable(Item obj, const char* name, int name_len) {
    if (get_type_id(obj) == LMD_TYPE_ARRAY && name_len == 6 &&
        strncmp(name, "length", 6) == 0) {
        ShapeEntry* se = js_find_shape_entry(obj, name, name_len);
        Map* m = js_obj_resolve_map(obj);
        return js_props_query_writable(m, se, name, name_len);
    }
    ShapeEntry* se = js_find_shape_entry(obj, name, name_len);
    Map* m = js_obj_resolve_map(obj);
    return js_props_query_writable(m, se, name, name_len);
}

extern "C" bool js_props_obj_query_configurable(Item obj, const char* name, int name_len) {
    if (get_type_id(obj) == LMD_TYPE_ARRAY && name_len == 6 &&
        strncmp(name, "length", 6) == 0) {
        return false;
    }
    ShapeEntry* se = js_find_shape_entry(obj, name, name_len);
    Map* m = js_obj_resolve_map(obj);
    return js_props_query_configurable(m, se, name, name_len);
}

// =============================================================================
// Stage A2.6 / A2-T5: attribute write helpers — shape-first, array fallback.
// =============================================================================
//
// Pre-A2-T5: each helper unconditionally wrote a `__nw_/__ne_/__nc_<name>`
// marker and a property-set hook propagated the marker into the corresponding
// `JSPD_NON_*` shape bit. That double-bookkeeping was needed because
// `ShapeEntry::flags` could be shared across sibling Maps (per-callsite shape
// cache) and an in-place mutation would corrupt them all.
//
// Post-A2-T5: with `js_shape_entry_update_flags` going through the Map-local
// TypeMap clone (A2-T1+T2), shape flags are reliably per-Map. Js59 P3 also
// materializes array numeric indices and `length` into companion-map
// ShapeEntry records before the flags are mutated. No helper writes
// `__nw_` / `__ne_` / `__nc_` marker slots.
static inline void js_attr_apply_shape_flags(Item obj, const char* name, int name_len,
                                             uint8_t set_mask, uint8_t clear_mask) {
    // Probe-first: if a shape entry exists, the clone-aware shape-flag path
    // is authoritative (and preferred — no map slot needed).
    ShapeEntry* se = js_find_shape_entry(obj, name, name_len);
    if (se) {
        js_shape_entry_update_flags(obj, name, name_len, set_mask, clear_mask);
        return;
    }
    if (js_attr_ensure_array_shape_entry(obj, name, name_len)) {
        js_shape_entry_update_flags(obj, name, name_len, set_mask, clear_mask);
    }
}

extern "C" void js_attr_set_writable(Item obj, const char* name, int name_len, bool writable) {
    if (writable) js_attr_apply_shape_flags(obj, name, name_len, 0, JSPD_NON_WRITABLE);
    else          js_attr_apply_shape_flags(obj, name, name_len, JSPD_NON_WRITABLE, 0);
}

extern "C" void js_attr_set_enumerable(Item obj, const char* name, int name_len, bool enumerable) {
    if (enumerable) js_attr_apply_shape_flags(obj, name, name_len, 0, JSPD_NON_ENUMERABLE);
    else            js_attr_apply_shape_flags(obj, name, name_len, JSPD_NON_ENUMERABLE, 0);
}

extern "C" void js_attr_set_configurable(Item obj, const char* name, int name_len, bool configurable) {
    if (configurable) js_attr_apply_shape_flags(obj, name, name_len, 0, JSPD_NON_CONFIGURABLE);
    else              js_attr_apply_shape_flags(obj, name, name_len, JSPD_NON_CONFIGURABLE, 0);
}

// =============================================================================
// Phase 3+4 Stage C: unified accessor producer (single-mode storage)
// =============================================================================
//
// Stage C: single-mode storage. Writes ONLY a JsAccessorPair Item under the
// actual property name X, with JSPD_IS_ACCESSOR + JSPD_NON_ENUMERABLE bits on
// the shape entry. Reader fast-paths in js_property_get / js_prototype_lookup
// / js_object_get_own_property_descriptor detect IS_ACCESSOR and dispatch via
// pair->getter directly (no snprintf, no separate slot, no legacy magic keys).
//
// The pair Item read raw has type_id=LMD_TYPE_FUNC (intentional, for tag-safety)
// so any code path that returns the slot as a value without checking the
// IS_ACCESSOR shape flag would deliver a fake Function. Mitigation: shape entry
// X is always marked NON_ENUMERABLE, keeping it out of for-in / Object.keys /
// JSON.stringify / Object spread.
extern "C" void js_install_native_accessor(Item obj, Item name, Item getter,
                                           Item setter, uint8_t attrs) {
    if (get_type_id(name) != LMD_TYPE_STRING) return;
    String* ns = it2s(name);
    if (!ns || ns->len == 0) return;

    int nl = (int)ns->len;
    if (nl > 248) return; // defensive bound retained for the transition helpers.

    // Allocate pair and store under name X. Use ItemNull as the slot for
    // missing getter/setter (per ES spec — absent half is undefined).
    Item g = (getter.item != ItemNull.item) ? getter : ItemNull;
    Item s = (setter.item != ItemNull.item) ? setter : ItemNull;
    JsAccessorPair* pair = js_alloc_accessor_pair(g, s);
    if (pair) {
        Item pair_item = js_accessor_pair_to_item(pair);
        js_property_set(obj, name, pair_item);
        // Set IS_ACCESSOR + force NON_ENUMERABLE on the shape entry so the
        // pair slot is not visible to enumeration/JSON/spread.
        uint8_t set_mask = JSPD_IS_ACCESSOR | JSPD_NON_ENUMERABLE;
        if (attrs & JSPD_NON_CONFIGURABLE) set_mask |= JSPD_NON_CONFIGURABLE;
        js_shape_entry_update_flags(obj, ns->chars, nl, set_mask, JSPD_DELETED);
    }

    // NON_CONFIGURABLE is encoded in the shape entry flags above.
    // JSPD_NON_WRITABLE is meaningless for accessors (ES spec); ignored.
}

// =============================================================================
// Phase 4: transpiler accessor producer (partial / merging)
// =============================================================================
//
// Merges getter or setter into an existing accessor pair under name X, or
// allocates a fresh pair if none exists. This handles the common transpiler
// pattern where `get x()` and `set x(v)` for the same property are emitted as
// separate top-level calls during class/object body traversal.
//
// Storage scheme is identical to js_install_native_accessor (Stage C):
//   - Slot at name X holds a JsAccessorPair* Item.
//   - Shape entry for X has JSPD_IS_ACCESSOR + caller-requested attrs bits.
//   - No legacy __get_X/__set_X writes.
static Map* js_obj_underlying_map(Item obj) {
    TypeId t = get_type_id(obj);
    if (t == LMD_TYPE_MAP) return obj.map;
    if (t == LMD_TYPE_ARRAY) {
        Array* arr = obj.array;
        return (arr && arr->extra != 0) ? (Map*)(uintptr_t)arr->extra : nullptr;
    }
    if (t == LMD_TYPE_FUNC) {
        JsFuncPropsView* fn = (JsFuncPropsView*)obj.function;
        if (!fn || fn->properties_map.item == 0) return nullptr;
        if (get_type_id(fn->properties_map) != LMD_TYPE_MAP) return nullptr;
        return fn->properties_map.map;
    }
    return nullptr;
}

static Item js_accessor_half_storage_value(Item value) {
    return get_type_id(value) == LMD_TYPE_UNDEFINED ? ItemNull : value;
}

static bool js_accessor_half_same(Item left, Item right) {
    left = js_accessor_half_storage_value(left);
    right = js_accessor_half_storage_value(right);
    if (left.item == ItemNull.item || right.item == ItemNull.item) {
        return left.item == right.item;
    }
    return it2b(js_object_is(left, right));
}

extern "C" void js_define_accessor_partial(Item obj, Item name, Item fn,
                                            int is_setter, uint8_t attrs) {
    if (get_type_id(name) != LMD_TYPE_STRING) return;
    String* ns = it2s(name);
    if (!ns) return;

    // Bypass setter accessor-dispatch in our own recursive js_property_set call:
    // we are storing the pair Item literally under name X, not invoking the
    // existing accessor (which would call pair->setter(pair_item) — wrong).
    // Stage D: RAII guard restores on all exits, including the early
    // `if (!pair) return;` path below.
    ScopedSkipAccessorDispatch _skip_guard;

    // Normalize "absent half" to ItemNull so read paths that gate on
    // `pair->getter.item != ItemNull.item` correctly treat an explicit-undefined
    // descriptor field (e.g. defineProperty with `{set: ...}` only) as absent.
    // Without this, Item-typed undefined leaks into pair->getter and dispatch
    // attempts to invoke `undefined` as a function.
    fn = js_accessor_half_storage_value(fn);

    // Look up any existing accessor pair under name X.
    JsAccessorPair* pair = nullptr;
    ShapeEntry* se = js_find_shape_entry(obj, ns->chars, (int)ns->len);
    if (se && jspd_is_accessor(se)) {
        Map* m = js_obj_underlying_map(obj);
        if (m) {
            bool found = false;
            Item slot_val = js_map_get_fast_ext(m, ns->chars, (int)ns->len, &found);
            if (found && slot_val.item != ItemNull.item) {
                pair = js_item_to_accessor_pair(slot_val);
            }
        }
    }
    if (se && !jspd_is_configurable(se)) {
        if (!jspd_is_accessor(se)) {
            js_throw_type_error("Cannot redefine property");
            return;
        }
        Item current_half = ItemNull;
        if (pair) current_half = is_setter ? pair->setter : pair->getter;
        if (!js_accessor_half_same(current_half, fn)) {
            js_throw_type_error("Cannot redefine property");
            return;
        }
    }

    if (pair) {
        // Merge into existing pair (in-place mutation; pair pointer unchanged).
        if (is_setter) pair->setter = fn;
        else           pair->getter = fn;
        // Re-store to keep slot value canonical (idempotent — same pointer bits).
        Item pair_item = js_accessor_pair_to_item(pair);
        js_property_set(obj, name, pair_item);
    } else {
        // Allocate fresh pair with the requested half populated.
        Item g = is_setter ? ItemNull : fn;
        Item s = is_setter ? fn       : ItemNull;
        pair = js_alloc_accessor_pair(g, s);
        if (!pair) return;
        Item pair_item = js_accessor_pair_to_item(pair);
        js_property_set(obj, name, pair_item);
    }

    // Set IS_ACCESSOR + caller-requested attribute bits on the shape entry.
    uint8_t set_mask = JSPD_IS_ACCESSOR;
    if (attrs & JSPD_NON_ENUMERABLE)   set_mask |= JSPD_NON_ENUMERABLE;
    if (attrs & JSPD_NON_CONFIGURABLE) set_mask |= JSPD_NON_CONFIGURABLE;
    js_shape_entry_update_flags(obj, ns->chars, (int)ns->len, set_mask, JSPD_DELETED);
}

// Phase-5C: 4-arg MIR-friendly wrapper. Returns `obj` so transpiler call sites
// can drop the result on the floor without needing a void-returning helper.
extern "C" Item js_to_property_key(Item key);
extern "C" Item js_install_user_accessor(Item obj, Item name, Item fn,
                                          int is_setter) {
    // Canonicalize the property key per ES §7.1.14 ToPropertyKey: numeric/bool
    // literal keys (e.g. `{ get [1]() {} }`) must be converted to their string
    // form before the chokepoint stores under that name.
    name = js_to_property_key(name);
    js_define_accessor_partial(obj, name, fn, is_setter, 0);
    return obj;
}

extern "C" Item js_get_prototype(Item object);
extern "C" Item js_get_prototype_of(Item object);

extern "C" JsAccessorPair* js_find_accessor_pair_inheritable(Item obj,
                                                              const char* name,
                                                              int name_len) {
    Item cur = obj;
    int depth = 0;
    while (depth < 16) {
        ShapeEntry* se = js_find_shape_entry(cur, name, name_len);
        if (se) {
            if (jspd_is_accessor(se)) {
                Map* m = js_obj_underlying_map(cur);
                if (m) {
                    bool found = false;
                    Item slot_val = js_map_get_fast_ext(m, name, name_len, &found);
                    if (found && slot_val.item != ItemNull.item) {
                        return js_item_to_accessor_pair(slot_val);
                    }
                }
            }
            // ES OrdinarySet: an own property (data or accessor) terminates
            // the lookup. Don't walk past an own data property to inherited
            // accessors — that would route writes through the inherited
            // setter instead of the receiver's own data slot.
            return nullptr;
        }
        if (get_type_id(cur) != LMD_TYPE_MAP) break;
        Item proto = js_get_prototype_of(cur);
        if (proto.item == ItemNull.item ||
            get_type_id(proto) == LMD_TYPE_UNDEFINED ||
            get_type_id(proto) == LMD_TYPE_NULL) break;
        cur = proto;
        depth++;
    }
    return nullptr;
}
