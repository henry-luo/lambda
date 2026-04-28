/**
 * Phase 1a foundation: JS property attribute and accessor primitives.
 * See js_property_attrs.h for design notes.
 */

#include "js_property_attrs.h"
#include "js_runtime.h"
#include "../lambda-data.hpp"
#include <string.h>
#include <stdio.h>

extern "C" void* heap_calloc(size_t size, TypeId type);
String* heap_create_name(const char* name, size_t len);

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
static TypeMap* js_obj_typemap(Item obj) {
    TypeId t = get_type_id(obj);
    if (t == LMD_TYPE_MAP) {
        Map* m = obj.map;
        return m ? (TypeMap*)m->type : nullptr;
    }
    if (t == LMD_TYPE_ARRAY) {
        Array* arr = obj.array;
        if (!arr || arr->extra == 0) return nullptr;
        Map* m = (Map*)(uintptr_t)arr->extra;
        return m ? (TypeMap*)m->type : nullptr;
    }
    if (t == LMD_TYPE_FUNC) {
        JsFuncPropsView* fn = (JsFuncPropsView*)obj.function;
        if (!fn || fn->properties_map.item == 0) return nullptr;
        if (get_type_id(fn->properties_map) != LMD_TYPE_MAP) return nullptr;
        Map* m = fn->properties_map.map;
        return m ? (TypeMap*)m->type : nullptr;
    }
    return nullptr;
}

extern "C" ShapeEntry* js_find_shape_entry(Item obj, const char* name, int name_len) {
    TypeMap* tm = js_obj_typemap(obj);
    if (!tm) return nullptr;
    // Fast path via hash table when populated.
    ShapeEntry* hit = typemap_hash_lookup(tm, name, name_len);
    if (hit) return hit;
    // Fallback linear scan (in case hash table not populated for this map).
    for (ShapeEntry* e = tm->shape; e; e = e->next) {
        if (e->name && (int)e->name->length == name_len &&
            memcmp(e->name->str, name, name_len) == 0) {
            return e;
        }
    }
    return nullptr;
}

extern "C" void js_shape_entry_update_flags(Item obj, const char* name, int name_len,
                                            uint8_t set_mask, uint8_t clear_mask) {
    ShapeEntry* se = js_find_shape_entry(obj, name, name_len);
    if (!se) return;
    se->flags = (uint8_t)((se->flags | set_mask) & ~clear_mask);
}

extern "C" bool js_dual_write_marker_flags(Item obj, Item key, Item value) {
    // Quick reject: only string keys can be markers.
    if (get_type_id(key) != LMD_TYPE_STRING) return false;
    String* ks = it2s(key);
    if (!ks || ks->len < 6) return false;
    const char* k = ks->chars;
    if (k[0] != '_' || k[1] != '_') return false;

    bool is_tombstone = (value.item == JS_DELETED_SENTINEL_VAL);
    bool is_truthy_val = !is_tombstone && js_is_truthy(value);
    int klen = (int)ks->len;
    uint8_t flag = 0;
    int prefix_len = 0;
    if      (klen >= 5 && memcmp(k, "__nw_", 5) == 0) { flag = JSPD_NON_WRITABLE;     prefix_len = 5; }
    else if (klen >= 5 && memcmp(k, "__ne_", 5) == 0) { flag = JSPD_NON_ENUMERABLE;   prefix_len = 5; }
    else if (klen >= 5 && memcmp(k, "__nc_", 5) == 0) { flag = JSPD_NON_CONFIGURABLE; prefix_len = 5; }
    else if (klen >= 6 && memcmp(k, "__get_", 6) == 0) { flag = JSPD_IS_ACCESSOR;     prefix_len = 6; }
    else if (klen >= 6 && memcmp(k, "__set_", 6) == 0) { flag = JSPD_IS_ACCESSOR;     prefix_len = 6; }
    else return false;

    const char* prop = k + prefix_len;
    int prop_len = klen - prefix_len;
    if (prop_len <= 0) return true; // matched marker prefix but no name; nothing to update

    // Boolean markers (nw/ne/nc): truthy → set, tombstone-or-falsy → clear.
    // Accessor markers (get/set): presence sets IS_ACCESSOR. Tombstoning a single
    // half (e.g. only __get_X) does NOT clear IS_ACCESSOR because the other half
    // may still exist; Phase 2b reader migration will properly handle clear.
    uint8_t set_mask = 0, clear_mask = 0;
    if (flag == JSPD_IS_ACCESSOR) {
        if (!is_tombstone) set_mask = JSPD_IS_ACCESSOR;
    } else {
        if (is_truthy_val) set_mask = flag;
        else               clear_mask = flag;
    }
    js_shape_entry_update_flags(obj, prop, prop_len, set_mask, clear_mask);
    return true;
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

    char buf[256];
    int nl = (int)ns->len;
    if (nl > (int)sizeof(buf) - 8) return; // defensive: overflow guard

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
        js_shape_entry_update_flags(obj, ns->chars, nl, set_mask, 0);
    }

    // Apply legacy NON_CONFIGURABLE marker if requested (separate magic key
    // scheme, unrelated to __get_X/__set_X). NON_ENUMERABLE is already encoded
    // in the shape entry flags above.
    if (attrs & JSPD_NON_CONFIGURABLE) {
        int len = snprintf(buf, sizeof(buf), "__nc_%.*s", nl, ns->chars);
        Item nck = (Item){.item = s2it(heap_create_name(buf, len))};
        js_property_set(obj, nck, (Item){.item = b2it(BOOL_TRUE)});
    }
    // JSPD_NON_WRITABLE is meaningless for accessors (ES spec); ignored.
}
