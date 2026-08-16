/**
 * Phase 1a foundation: JS property attribute and accessor primitives.
 * See js_property_attrs.h for design notes.
 */

#include "js_property_attrs.h"

extern "C" bool js_proto_snapshot_requires_typemap_detach(Item obj);
#include "js_props.h"
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_function.hpp"
#include "js_state_guards.h"
#include "../lambda.hpp"
#include "../lambda-data.hpp"
#include "../core/name_pool.hpp"
#include "../../lib/log.h"
#include <string.h>
#include <stdio.h>

extern __thread EvalContext* context;

String* heap_create_name(const char* name, size_t len);

extern "C" JsAccessorPair* js_alloc_accessor_pair(Item getter, Item setter) {
    // Allocate as LMD_TYPE_FUNC so the GC tracer (if any) treats it like a Function
    // and so `Item.type_id()` returns FUNC for tag-safety. Callers must rely on
    // ShapeEntry::flags JSPD_IS_ACCESSOR to disambiguate accessor pair from real
    // Function before invoking it.
    JsAccessorPair* p = (JsAccessorPair*)heap_calloc(sizeof(JsAccessorPair), LMD_TYPE_FUNC);
    if (!p) return nullptr;
    p->type_id = LMD_TYPE_FUNC;
    p->layout_magic = JS_ACCESSOR_PAIR_LAYOUT_MAGIC;
    p->getter = getter;
    p->setter = setter;
    return p;
}

// Locate the underlying TypeMap holding shape entries for a JS object.
// Arrays use a companion Map stored in their reserved props tail slot.
static TypeMap* js_obj_typemap(Item obj) {
    TypeId t = get_type_id(obj);
    if (t == LMD_TYPE_MAP) {
        Map* m = obj.map;
        if (!m) return nullptr;
        TypeMap* tm = (TypeMap*)m->type;
#ifndef NDEBUG
        // ordinary Map storage owns a valid TypeMap whenever it publishes one;
        // recovering as an absent shape would turn an internal lifetime bug
        // into an observable property miss.
        assert(!tm || typemap_ptr_is_plausible(tm));
#endif
        return tm;
    }
    if (t == LMD_TYPE_ARRAY || js_is_ordinary_numeric_array(obj)) {
        Array* arr = obj.array;
        if (!js_array_has_props(arr)) return nullptr;
        Map* m = js_array_props(arr);
        if (!m) return nullptr;
        TypeMap* tm = (TypeMap*)m->type;
#ifndef NDEBUG
        assert(!tm || typemap_ptr_is_plausible(tm));
#endif
        return tm;
    }
    if (t == LMD_TYPE_FUNC) {
        JsFunction* fn = (JsFunction*)obj.function;
        if (!fn || fn->properties_map.item == 0) return nullptr;
        if (get_type_id(fn->properties_map) != LMD_TYPE_MAP) return nullptr;
        Map* m = fn->properties_map.map;
        if (!m) return nullptr;
        TypeMap* tm = (TypeMap*)m->type;
#ifndef NDEBUG
        assert(!tm || typemap_ptr_is_plausible(tm));
#endif
        return tm;
    }
    return nullptr;
}

extern "C" ShapeEntry* js_find_shape_entry(Item obj, const char* name, int name_len) {
    TypeMap* tm = js_obj_typemap(obj);
    if (!tm) return nullptr;
    return typemap_hash_lookup(tm, name, name_len);
}

extern "C" ShapeEntry* js_find_shape_entry_name_id(Item obj, NameId name_id) {
    TypeMap* tm = js_obj_typemap(obj);
    if (!tm || name_id == NAME_ID_NONE) return nullptr;
    NameRef name = name_pool_resolve_id(context ? context->name_pool : NULL,
        name_id);
    ShapeEntry* entry = typemap_hash_lookup_by_name_id(tm, name_id,
        property_key_hash(name));
    if (entry) return entry;
    // An id-bearing lookup can still address an id-less Input field. The
    // resolved ordinary spelling is only a byte-seam fallback; it must not
    // select a different generated property that happens to share its text.
    return name && property_key_kind(name) == NAME_KEY_STRING
        ? typemap_hash_lookup_idless(tm, name->chars, (int)name->len) : nullptr;
}

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
static TypeMap* js_typemap_clone_for_mutation_ex(Item obj, bool force_clone) {
    Map* underlying = js_obj_underlying_map(obj);
    if (!underlying) return nullptr;
    TypeMap* tm = (TypeMap*)underlying->type;
    if (!tm) return nullptr;
    if (tm->is_private_clone && !force_clone) return tm;
    if (!js_input || !js_input->pool) return nullptr;
    Pool* pool = js_input->pool;

    TypeMap* clone = (TypeMap*)alloc_type(pool, LMD_TYPE_MAP, sizeof(TypeMap));
    if (!clone) return nullptr;
    clone->length = tm->length;
    clone->byte_size = tm->byte_size;
    clone->type_index = tm->type_index;
    clone->has_named_shape = tm->has_named_shape;
    clone->is_trusted_contract = false;
    clone->struct_name = tm->struct_name;
    clone->is_private_clone = true;
    clone->is_shared_constructor_shape = false;
    clone->is_transition_shared_shape = false;
    clone->transitions = NULL;
    // Tune6: descriptor cloning preserves the immutable semantic family; a
    // shape mutation must never silently turn an instance into another class.
    clone->js_meta = tm->js_meta;
    clone->has_array_index_shape = tm->has_array_index_shape;

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
        dst->name_hash = src->name_hash;
        dst->name_id = src->name_id;
        dst->key_kind = src->key_kind;
        dst->flags = src->flags;
        if (!first_clone) first_clone = dst;
        if (prev_clone) prev_clone->next = dst;
        prev_clone = dst;
        last_clone = dst;
    }
    clone->shape = first_clone;
    clone->last = last_clone;

    // Repopulate the per-TypeMap field_index hash against the cloned entries.
    // Dynamic tables are pool-owned, so clones must never alias the source index.
    typemap_hash_build(clone, pool);

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

// a snapshot Map may already own a private TypeMap. Private ownership is
// normally enough, but its shape is the reset blueprint while that exact
// TypeMap is installed; detach before changing it (D6.2.2v2).
JS_FORWARD_STATIC_RETURN(TypeMap*, js_typemap_clone_for_mutation, (Item obj),
    js_typemap_clone_for_mutation_ex,
    (obj, js_proto_snapshot_requires_typemap_detach(obj)))

static void js_shape_entry_update_flags_impl(Item obj, NameId name_id,
        const char* name, int name_len, uint8_t set_mask, uint8_t clear_mask) {
    if (set_mask == 0 && clear_mask == 0) return;
    // Probe first: if the entry doesn't exist or the mutation is a no-op,
    // skip cloning entirely. This avoids replacing m->type with a fresh
    // TypeMap on Maps whose original type was &EmptyMap (or otherwise
    // length==0) — which would later strand map_put because it only
    // initializes mp->data/data_cap when `!mp->type` and our non-null clone
    // bypasses that init path.
    ShapeEntry* se = name_id != NAME_ID_NONE
        ? js_find_shape_entry_name_id(obj, name_id)
        : js_find_shape_entry(obj, name, name_len);
    if (!se) return;
    uint8_t new_flags = (uint8_t)((se->flags | set_mask) & ~clear_mask);
    if (new_flags == se->flags) return;
    // A2-T2: detach this Map's TypeMap from any siblings before mutating
    // ShapeEntry::flags. If cloning isn't possible (no js_input pool, or no
    // mutable underlying Map), fall back to in-place mutation — preserves
    // pre-clone behavior on edge paths.
    if (js_typemap_clone_for_mutation(obj)) {
        se = name_id != NAME_ID_NONE
            ? js_find_shape_entry_name_id(obj, name_id)
            : js_find_shape_entry(obj, name, name_len);
        if (!se) return;
    }
    js_map_promote_descriptor_kind(js_obj_underlying_map(obj));
    se->flags = new_flags;
}
JS_FORWARD_VOID( js_shape_entry_update_flags, (Item obj, const char* name, int name_len,                                             uint8_t set_mask, uint8_t clear_mask), js_shape_entry_update_flags_impl, (obj, NAME_ID_NONE, name, name_len, set_mask, clear_mask))

extern "C" void js_shape_entry_update_flags_name_id(Item obj, NameId name_id,
        uint8_t set_mask, uint8_t clear_mask) {
    if (name_id == NAME_ID_NONE) return;
    js_shape_entry_update_flags_impl(obj, name_id, NULL, 0,
        set_mask, clear_mask);
}

// Public wrapper around the file-static TypeMap clone primitive. Exposed so
// metadata selection can detach a shared shape without duplicating it here.
JS_FORWARD_RETURN(TypeMap*, js_typemap_clone_for_mutation_pub, (Item obj),
    js_typemap_clone_for_mutation, (obj))

JS_FORWARD_EXPRESSION(bool, js_typemap_detach_snapshot_for_mutation, (Item obj),
    !js_proto_snapshot_requires_typemap_detach(obj) ||
        js_typemap_clone_for_mutation_ex(obj, /*force_clone=*/true) != NULL)

static bool js_typemap_transition_matches(const TypeMapTransition* transition,
        const ShapeEntry* entry, NameId operation_name_id, TypeId value_type) {
    if (!transition || !entry || !entry->name || !entry->name->str ||
            transition->value_type != value_type) {
        return false;
    }
    if (transition->name_id != NAME_ID_NONE) {
        return transition->name_id == operation_name_id;
    }
    return transition->key_kind == NAME_KEY_STRING &&
        entry->key_kind == NAME_KEY_STRING &&
        transition->name_len == entry->name->length &&
        memcmp(transition->name, entry->name->str, transition->name_len) == 0;
}

static ShapeEntry* js_typemap_transition_entry(TypeMap* target,
        const ShapeEntry* source) {
    if (!target || !source || !source->name || !source->name->str) return NULL;
    if (source->name_id != NAME_ID_NONE) {
        NameRef name = name_pool_resolve_id(context ? context->name_pool : NULL,
            source->name_id);
        return typemap_hash_lookup_by_name_id(target, source->name_id,
            property_key_hash(name));
    }
    return typemap_hash_lookup(target, source->name->str, (int)source->name->length);
}

extern "C" TypeMap* js_typemap_transition_for_type(Item obj,
        ShapeEntry* entry, NameId operation_name_id, TypeId value_type) {
    Map* underlying = js_obj_underlying_map(obj);
    if (!underlying || !entry || !entry->name || !entry->name->str ||
            !js_input || !js_input->pool) {
        return NULL;
    }
    TypeMap* source = (TypeMap*)underlying->type;
    if (!source || !typemap_is_shared_shape(source) || !entry->type ||
            entry->type->type_id == value_type) {
        return source;
    }

    TypeId storage_type = shape_entry_storage_type_id(entry);
    if (!typemap_entry_uses_fixed_slot(source, entry) &&
            type_info[storage_type].byte_size != type_info[value_type].byte_size) {
        // A cached transition may retag a fixed-width constructor slot, but a
        // packed field can move every following offset.  Reusing its old
        // offset would overwrite the next property; make the caller rebuild.
        return NULL;
    }

    for (TypeMapTransition* transition = source->transitions; transition;
            transition = transition->next) {
        if (js_typemap_transition_matches(transition, entry,
                operation_name_id, value_type) &&
                transition->target) {
            underlying->type = transition->target;
            return transition->target;
        }
    }

    TypeMapTransition* transition = (TypeMapTransition*)pool_calloc(js_input->pool,
        sizeof(TypeMapTransition));
    if (!transition) return NULL;

    TypeMap* target = js_typemap_clone_for_mutation(obj);
    if (!target) return NULL;
    ShapeEntry* target_entry = js_typemap_transition_entry(target, entry);
    if (!target_entry) {
        underlying->type = source;
        return NULL;
    }

    // The source blueprint must stay immutable: a mixed null/Array field is
    // common in JS constructors, and retagging it forced every later instance
    // into a private pool-owned TypeMap clone.
    target->is_private_clone = false;
    target->is_shared_constructor_shape = false;
    target->is_transition_shared_shape = true;
    target->transitions = NULL;
    target_entry->type = type_info[value_type].type;

    transition->name_id = operation_name_id != NAME_ID_NONE
        ? operation_name_id : entry->name_id;
    transition->key_kind = entry->key_kind;
    transition->name = transition->name_id == NAME_ID_NONE ? entry->name->str : NULL;
    transition->name_len = transition->name_id == NAME_ID_NONE
        ? (uint32_t)entry->name->length : 0;
    transition->value_type = value_type;
    transition->flags = entry->flags;
    transition->target = target;
    transition->next = source->transitions;
    source->transitions = transition;
    return target;
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

// Tombstone bit mutator. Same per-Map clone safety as the accessor helper.
// Bit-only: ordinary map/property deletes do not store the raw dense-array
// hole sentinel in typed map slots. When the property has no shape entry yet
// this is a no-op; callers that need a virtual tombstone should materialize a
// safe slot first via js_shape_mark_deleted_own(..., create_if_missing=true).
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

JS_FORWARD_STATIC_EXPRESSION(bool, js_attrs_name_is_length,
    (const char* name, int name_len),
    name && name_len == 6 && memcmp(name, "length", 6) == 0)

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
    if (!js_array_has_props(arr)) {
        Item obj = js_new_object();
        obj.map->map_kind = MAP_KIND_ARRAY_PROPS;
        js_elements_set_props(arr, obj.map);
    }
    return js_array_props(arr);
}

static void js_attr_mark_array_index_shape(Item target, const char* name, int name_len) {
    if (!js_attrs_name_is_digits(name, name_len)) return;
    Map* props = NULL;
    if (get_type_id(target) == LMD_TYPE_ARRAY ||
            js_is_ordinary_numeric_array(target)) {
        Array* arr = target.array;
        if (!js_array_has_props(arr)) return;
        props = js_array_props(arr);
    } else if (get_type_id(target) == LMD_TYPE_MAP) {
        props = target.map;
    }
    if (!props || !map_kind_is_array_props(props->map_kind)) {
        return;
    }
    TypeMap* tm = (TypeMap*)props->type;
    if (tm) tm->has_array_index_shape = true;
}

static bool js_attr_ensure_array_shape_entry(Item obj, const char* name, int name_len) {
    if (!name || name_len <= 0 || name_len >= 240) return false;
    if (!js_attrs_name_is_digits(name, name_len) &&
        !js_attrs_name_is_length(name, name_len)) return false;

    Item target = ItemNull;
    Array* arr = nullptr;
    TypeId type = get_type_id(obj);
    if (type == LMD_TYPE_ARRAY || js_is_ordinary_numeric_array(obj)) {
        arr = obj.array;
        Map* pm = js_attr_ensure_array_props_map(arr);
        if (!pm) return false;
        target = (Item){.map = pm};
    } else if (type == LMD_TYPE_MAP && obj.map &&
               map_kind_is_array_props(obj.map->map_kind)) {
        target = obj;
    } else {
        return false;
    }

    if (js_find_shape_entry(target, name, name_len)) {
        js_attr_mark_array_index_shape(target, name, name_len);
        return true;
    }

    Item name_item = js_name_item(name, (size_t)name_len);
    Item slot_value = (Item){.item = ITEM_JS_UNDEFINED};
    bool slot_found = false;
    if (get_type_id(target) == LMD_TYPE_MAP) {
        JsShapeSlotStatus status = js_own_shape_slot_status(target, name, name_len, &slot_value, NULL);
        slot_found = (status == JS_SHAPE_SLOT_DATA || status == JS_SHAPE_SLOT_ACCESSOR);
    }
    if (!slot_found && arr && js_attrs_name_is_digits(name, name_len)) {
        int64_t idx = js_attrs_parse_index_name(name, name_len);
        if (idx >= 0 && idx < arr->length && idx < container_dense_capacity(arr) &&
            arr->items[idx].item != JS_DELETED_SENTINEL_VAL) {
            slot_value = arr->items[idx];
            slot_found = true;
        } else if (idx >= 0 && js_array_sparse_has_index(obj, idx)) {
            slot_value = js_array_sparse_get_index(obj, idx);
            slot_found = true;
        }
    }

    if (get_type_id(target) == LMD_TYPE_MAP && js_input) {
        map_put_heap(target.map, it2s(name_item), slot_value, js_input);
    } else {
        js_define_own_key_storage(target, name_item, slot_value);
    }
    if (!js_find_shape_entry(target, name, name_len)) return false;
    js_attr_mark_array_index_shape(target, name, name_len);
    if (arr && js_attrs_name_is_digits(name, name_len)) {
        int64_t idx = js_attrs_parse_index_name(name, name_len);
        if (idx >= 0 && idx < arr->length && idx < container_dense_capacity(arr)) {
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
// the reserved array props slot). Returns nullptr if the object has no map storage.
static Map* js_obj_resolve_map(Item obj) {
    TypeId t = get_type_id(obj);
    if (t == LMD_TYPE_MAP) return obj.map;
    if (t == LMD_TYPE_FUNC) {
        JsFunction* fn = (JsFunction*)obj.function;
        if (!fn || fn->properties_map.item == 0) return nullptr;
        if (get_type_id(fn->properties_map) != LMD_TYPE_MAP) return nullptr;
        return fn->properties_map.map;
    }
    if (t == LMD_TYPE_ARRAY || js_is_ordinary_numeric_array(obj)) {
        Array* arr = obj.array;
        if (!js_array_has_props(arr)) return nullptr;
        return js_array_props(arr);
    }
    return nullptr;
}

typedef bool (*JsPropsQuery)(Map*, ShapeEntry*, const char*, int);

static bool js_props_obj_query(Item obj, const char* name, int name_len,
        JsPropsQuery query, bool override_array_length, bool array_length_value) {
    if ((get_type_id(obj) == LMD_TYPE_ARRAY ||
            js_is_ordinary_numeric_array(obj)) && name_len == 6 &&
        strncmp(name, "length", 6) == 0) {
        if (override_array_length) return array_length_value;
    }
    ShapeEntry* se = js_find_shape_entry(obj, name, name_len);
    Map* m = js_obj_resolve_map(obj);
    return query(m, se, name, name_len);
}
JS_FORWARD_RETURN(bool, js_props_obj_query_enumerable, (Item obj, const char* name, int name_len), js_props_obj_query, (obj, name, name_len, js_props_query_enumerable, true, false))
JS_FORWARD_RETURN(bool, js_props_obj_query_writable, (Item obj, const char* name, int name_len), js_props_obj_query, (obj, name, name_len, js_props_query_writable, false, false))
JS_FORWARD_RETURN(bool, js_props_obj_query_configurable, (Item obj, const char* name, int name_len), js_props_obj_query, (obj, name, name_len, js_props_query_configurable, true, false))

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
        js_attr_mark_array_index_shape(obj, name, name_len);
        return;
    }
    if (js_attr_ensure_array_shape_entry(obj, name, name_len)) {
        js_shape_entry_update_flags(obj, name, name_len, set_mask, clear_mask);
        js_attr_mark_array_index_shape(obj, name, name_len);
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

static Item js_store_accessor_pair_slot(Item obj, Item name, Item pair) {
    if (get_type_id(obj) == LMD_TYPE_FUNC) {
        // Function name/length are non-writable but configurable. Descriptor
        // replacement is [[DefineOwnProperty]], so routing the pair through
        // ordinary [[Set]] leaves the old data value under an accessor shape.
        js_func_init_property(obj, name, pair);
        return js_status_ok();
    }
    return js_define_own_key_storage(obj, name, pair);
}

// =============================================================================
// Phase 3+4 Stage C: unified accessor producer (single-mode storage)
// =============================================================================
//
// Stage C: single-mode storage. Writes ONLY a JsAccessorPair Item under the
// actual property name X, with JSPD_IS_ACCESSOR + JSPD_NON_ENUMERABLE bits on
// the shape entry. Reader fast-paths in js_get_key_default / js_prototype_lookup
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
    JS_ROOTS(roots,
        obj_root, obj,
        name_root, name,
        getter_root, getter,
        setter_root, setter,
        pair_root, ItemNull);

    String* ns = it2s(name_root.get());
    if (!ns || ns->len == 0) return;

    int nl = (int)ns->len;
    if (nl > 248) return; // defensive bound retained for the transition helpers.

    // Allocate pair and store under name X. Use ItemNull as the slot for
    // missing getter/setter (per ES spec — absent half is undefined).
    Item g = (getter_root.get().item != ItemNull.item) ? getter_root.get() : ItemNull;
    Item s = (setter_root.get().item != ItemNull.item) ? setter_root.get() : ItemNull;
    JsAccessorPair* pair = js_alloc_accessor_pair(g, s);
    if (pair) {
        // The accessor installation path can allocate repeatedly; keep every
        // participating value rooted until both the slot and shape are durable.
        pair_root.set(js_accessor_pair_to_item(pair));
        js_store_accessor_pair_slot(obj_root.get(), name_root.get(), pair_root.get());
        // Set IS_ACCESSOR + force NON_ENUMERABLE on the shape entry so the
        // pair slot is not visible to enumeration/JSON/spread.
        uint8_t set_mask = JSPD_IS_ACCESSOR | JSPD_NON_ENUMERABLE;
        if (attrs & JSPD_NON_CONFIGURABLE) set_mask |= JSPD_NON_CONFIGURABLE;
        ns = it2s(name_root.get());
        if (property_key_requires_identity(ns)) {
            // A Symbol/private accessor slot is addressed only by its record;
            // a byte update would leave the raw pair visible to ordinary get.
            js_shape_entry_update_flags_name_id(obj_root.get(),
                property_key_id(ns), set_mask, JSPD_DELETED);
        } else {
            js_shape_entry_update_flags(obj_root.get(), ns->chars, nl, set_mask, JSPD_DELETED);
        }
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
extern "C" Map* js_obj_underlying_map(Item obj) {
    TypeId t = get_type_id(obj);
    if (t == LMD_TYPE_MAP) return obj.map;
    if (t == LMD_TYPE_ARRAY || js_is_ordinary_numeric_array(obj)) {
        Array* arr = obj.array;
        return js_array_props(arr);
    }
    if (t == LMD_TYPE_FUNC) {
        JsFunction* fn = (JsFunction*)obj.function;
        if (!fn || fn->properties_map.item == 0) return nullptr;
        if (get_type_id(fn->properties_map) != LMD_TYPE_MAP) return nullptr;
        return fn->properties_map.map;
    }
    return nullptr;
}
JS_FORWARD_STATIC_EXPRESSION(Item, js_accessor_half_storage_value, (Item value), (get_type_id(value) == LMD_TYPE_UNDEFINED ? ItemNull : value))

static bool js_accessor_half_same(Item left, Item right) {
    left = js_accessor_half_storage_value(left);
    right = js_accessor_half_storage_value(right);
    if (left.item == ItemNull.item || right.item == ItemNull.item) {
        return left.item == right.item;
    }
    return it2b(js_object_is(left, right));
}

extern "C" Item js_define_accessor_partial(Item obj, Item name, Item fn,
                                            int is_setter, uint8_t attrs) {
    JS_ROOTS(roots,
        obj_root, obj,
        name_root, name,
        fn_root, fn,
        pair_root, ItemNull,
        getter_root, ItemNull,
        setter_root, ItemNull);
    obj = obj_root.get();
    name = name_root.get();
    fn = fn_root.get();
    if (get_type_id(name) != LMD_TYPE_STRING) return js_status_ok();
    String* ns = it2s(name);
    if (!ns) return js_status_ok();

    // Accessor installation can add a new shape before setting its descriptor
    // flags. Detach a snapshot-backed target so the intrinsic blueprint stays
    // immutable across hot-batch realm resets (D6.2.2v2).
    js_typemap_clone_for_mutation(obj_root.get());

    // Normalize "absent half" to ItemNull so read paths that gate on
    // `pair->getter.item != ItemNull.item` correctly treat an explicit-undefined
    // descriptor field (e.g. defineProperty with `{set: ...}` only) as absent.
    // Without this, Item-typed undefined leaks into pair->getter and dispatch
    // attempts to invoke `undefined` as a function.
    fn_root.set(js_accessor_half_storage_value(fn));
    fn = fn_root.get();

    // Look up any existing accessor pair under name X.
    // Every pooled NameId, including ordinary static/dynamic spellings, must
    // remain on the exact-identity path.  Only id-less Input fields may use
    // bytes to find their descriptor.
    bool identity_key = property_key_id(ns) != NAME_ID_NONE;
    JsAccessorPair* pair = nullptr;
    NameId identity_id = identity_key ? property_key_id(ns) : NAME_ID_NONE;
    ShapeEntry* se = identity_key ? js_find_shape_entry_name_id(obj, identity_id) :
        js_find_shape_entry(obj, ns->chars, (int)ns->len);
    if (se && jspd_is_accessor(se)) {
        Item slot_val = ItemNull;
        JsShapeSlotStatus status = identity_key
            ? js_own_shape_slot_status_name_id(obj, identity_id, &slot_val, NULL)
            : js_own_shape_slot_status(obj, ns->chars, (int)ns->len, &slot_val, NULL);
        if (status == JS_SHAPE_SLOT_ACCESSOR && slot_val.item != ItemNull.item) {
            pair = js_item_to_accessor_pair(slot_val);
        }
    }
    if (se && !jspd_is_configurable(se)) {
        if (!jspd_is_accessor(se)) {
            return js_throw_type_error("Cannot redefine property");
        }
        Item current_half = ItemNull;
        if (pair) current_half = is_setter ? pair->setter : pair->getter;
        if (!js_accessor_half_same(current_half, fn)) {
            return js_throw_type_error("Cannot redefine property");
        }
    }

    if (pair) {
        getter_root.set(pair->getter);
        setter_root.set(pair->setter);
    }
    if (is_setter) setter_root.set(fn_root.get());
    else           getter_root.set(fn_root.get());
    // Accessor pairs are mutable carriers outside the owning Map's data
    // image. Replacing the pair keeps an intrinsic snapshot's native accessor
    // intact when defineProperty installs a test-local getter (D6.2.2v2).
    pair = js_alloc_accessor_pair(getter_root.get(), setter_root.get());
    if (!pair) return js_throw_error_with_code("ERR_RUNTIME_FAILURE",
                                               "accessor pair allocation failed");
    pair_root.set(js_accessor_pair_to_item(pair));
    JS_ASSIGN_OR_RETURN(set_result, js_store_accessor_pair_slot(
        obj_root.get(), name_root.get(), pair_root.get()));

    // Set IS_ACCESSOR + caller-requested attribute bits on the shape entry.
    uint8_t set_mask = JSPD_IS_ACCESSOR;
    if (attrs & JSPD_NON_ENUMERABLE)   set_mask |= JSPD_NON_ENUMERABLE;
    if (attrs & JSPD_NON_CONFIGURABLE) set_mask |= JSPD_NON_CONFIGURABLE;
    if (identity_key) {
        // Symbol text is diagnostic-only; mutate the exact installed key.
        js_shape_entry_update_flags_name_id(obj, identity_id, set_mask,
            JSPD_DELETED);
    } else {
        js_shape_entry_update_flags(obj, ns->chars, (int)ns->len, set_mask, JSPD_DELETED);
    }
    // D3.4.4v2: pooled string identity does not erase array-index shape facts.
    // Missing this mark let dense stores bypass numeric accessors installed in
    // an Array companion map through the NameId branch.
    js_attr_mark_array_index_shape(obj, ns->chars, (int)ns->len);
    return js_status_ok();
}

// Phase-5C: 4-arg MIR-friendly wrapper. Returns `obj` so transpiler call sites
// can drop the result on the floor without needing a void-returning helper.
extern "C" Item js_install_user_accessor(Item obj, Item name, Item fn,
                                          int is_setter) {
    JS_ROOTS(roots, obj_root, obj, name_root, name, fn_root, fn);
    // Canonicalize the property key per ES §7.1.14 ToPropertyKey: numeric/bool
    // literal keys (e.g. `{ get [1]() {} }`) must be converted to their string
    // form before the chokepoint stores under that name.
    JS_ASSIGN_OR_RETURN(key_result, js_to_property_key(name_root.get()));
    name_root.set(key_result);
    JS_ASSIGN_OR_RETURN(accessor_result, js_define_accessor_partial(
        obj_root.get(), name_root.get(), fn_root.get(), is_setter, 0));
    return obj_root.get();
}



extern "C" JsAccessorPair* js_find_accessor_pair_inheritable_name_id(Item obj,
        NameId name_id) {
    if (name_id == NAME_ID_NONE) return nullptr;
    RootFrame roots(1);
    Rooted<Item> cur(roots, obj);
    int depth = 0;
    while (depth < 16) {
        ShapeEntry* se = js_find_shape_entry_name_id(cur.get(), name_id);
        Map* m = se ? js_obj_underlying_map(cur.get()) : NULL;
        if (se && m && map_ctor_offset_is_reserved(m, se->byte_offset)) se = NULL;
        if (se) {
            if (jspd_is_accessor(se)) {
                Item slot_val = ItemNull;
                if (js_own_shape_slot_status_name_id(cur.get(), name_id,
                        &slot_val, NULL) == JS_SHAPE_SLOT_ACCESSOR &&
                        slot_val.item != ItemNull.item) {
                    return js_item_to_accessor_pair(slot_val);
                }
            }
            return nullptr;
        }
        TypeId cur_type = get_type_id(cur.get());
        if (cur_type != LMD_TYPE_MAP && cur_type != LMD_TYPE_ARRAY &&
                cur_type != LMD_TYPE_FUNC && cur_type != LMD_TYPE_ELEMENT) break;
        // Function.prototype is itself callable. Stopping at non-Map values
        // skipped its inherited %ThrowTypeError% accessors and let assignments
        // create own caller/arguments properties instead.
        Item proto = js_get_prototype_of(cur.get());
        if (proto.item == ItemNull.item || get_type_id(proto) == LMD_TYPE_UNDEFINED ||
                get_type_id(proto) == LMD_TYPE_NULL) break;
        cur.set(proto);
        depth++;
    }
    return nullptr;
}

extern "C" JsAccessorPair* js_find_accessor_pair_inheritable(Item obj,
                                                              const char* name,
                                                              int name_len) {
    if (!name || name_len < 0) return nullptr;
    NameRef key = heap_create_name(name, (size_t)name_len);
    return key ? js_find_accessor_pair_inheritable_name_id(obj,
        property_key_id(key)) : nullptr;
}
