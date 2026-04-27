/**
 * js_dom_selection.cpp — JS bindings for DOM Range / Selection
 *
 * Wraps radiant/dom_range.{hpp,cpp} primitives as JS host objects and
 * routes document.createRange(), document.getSelection(), and the global
 * getSelection() through them.
 *
 * Per-Range / per-Selection state is stored in flat pools indexed by a
 * hidden "__range_id" / "__sel_id" property on the JS object. Methods
 * recover the receiver via js_get_this() and look up native state.
 *
 * The selection / live-range list lives on the document's RadiantState
 * (DomDocument::state). In headless JS mode that may be NULL on entry, so
 * we lazily create it via radiant_state_create() on first use.
 */

#include "js_dom_selection.h"
#include "js_dom.h"
#include "js_runtime.h"
#include "../lambda.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../input/css/dom_node.hpp"
#include "../input/css/dom_element.hpp"
#include "../../lib/log.h"

#include "../../radiant/dom_range.hpp"
#include "../../radiant/state_store.hpp"

#include <cstring>
#include <cstdlib>

// ============================================================================
// Helpers
// ============================================================================

static inline Item make_undef() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static inline Item make_bool(bool v) {
    return (Item){.item = b2it(v ? 1 : 0)};
}

static inline Item make_int(int64_t v) {
    return (Item){.item = i2it(v)};
}

static inline Item make_str(const char* s) {
    if (!s) return ItemNull;
    return (Item){.item = s2it(heap_create_name(s))};
}

static inline Item make_key(const char* s) { return make_str(s); }

static inline void prop_set(Item obj, const char* key, Item val) {
    js_property_set(obj, make_key(key), val);
}

static inline Item prop_get(Item obj, const char* key) {
    return js_property_get(obj, make_key(key));
}

static int item_to_int(Item v) {
    TypeId t = get_type_id(v);
    if (t == LMD_TYPE_INT) return (int)it2i(v);
    if (t == LMD_TYPE_FLOAT) {
        double* d = (double*)(v.item & 0x00FFFFFFFFFFFFFF);
        return d ? (int)*d : 0;
    }
    return 0;
}

static bool item_to_bool(Item v) {
    TypeId t = get_type_id(v);
    if (t == LMD_TYPE_BOOL) return (v.item & 0xFF) != 0;
    if (t == LMD_TYPE_NULL || t == LMD_TYPE_UNDEFINED) return false;
    if (t == LMD_TYPE_INT) return it2i(v) != 0;
    return true;
}

static void throw_dom_exception(const char* name, const char* msg) {
    Item n = make_str(name ? name : "Error");
    Item m = make_str(msg ? msg : "");
    js_throw_value(js_new_error_with_name(n, m));
}

// Translate dom_range exception strings (e.g. "InvalidNodeTypeError",
// "IndexSizeError", "WrongDocumentError", "HierarchyRequestError",
// "InvalidStateError") into a JS DOMException-like throw.
static void throw_from_dom_exc(const char* exc, const char* fallback_msg) {
    throw_dom_exception(exc ? exc : "InvalidStateError",
                        fallback_msg ? fallback_msg : "");
}

// ============================================================================
// Per-document RadiantState (lazy in headless JS mode)
// ============================================================================

static RadiantState* get_or_create_state() {
    DomDocument* doc = (DomDocument*)js_dom_get_document();
    if (!doc) return nullptr;
    if (doc->state) return doc->state;
    if (!doc->pool) return nullptr;
    doc->state = radiant_state_create(doc->pool, STATE_MODE_IN_PLACE);
    return doc->state;
}

// ============================================================================
// Range / Selection wrappers — DOM-style: Map::data holds the native pointer,
// Map::type holds a sentinel marker, map_kind=MAP_KIND_DOM dispatches all
// property reads/writes to js_dom_get_property / js_dom_set_property, which
// route by marker into js_dom_range_get_property / js_dom_selection_get_property.
// No JS-visible "private" properties; no shape allocations on the wrapper.
// ============================================================================

// Sentinel TypeMap addresses — only their identity matters.
TypeMap js_dom_range_marker     = {};
TypeMap js_dom_selection_marker = {};

// Forward decls
static Item build_range_object(DomRange* r);
static Item build_selection_object(DomSelection* s);
static inline void range_sync_props(Item, DomRange*);
static inline void selection_sync_props(Item, DomSelection*);

// Identity / unwrap ----------------------------------------------------------

extern "C" bool js_dom_item_is_range(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m && m->map_kind == MAP_KIND_DOM &&
           m->type == (void*)&js_dom_range_marker;
}

extern "C" bool js_dom_item_is_selection(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m && m->map_kind == MAP_KIND_DOM &&
           m->type == (void*)&js_dom_selection_marker;
}

static inline DomRange* range_from(Item obj) {
    return js_dom_item_is_range(obj) ? (DomRange*)obj.map->data : nullptr;
}
static inline DomSelection* selection_from(Item obj) {
    return js_dom_item_is_selection(obj) ? (DomSelection*)obj.map->data : nullptr;
}
static inline DomRange*     range_from_this()     { return range_from(js_get_this()); }
static inline DomSelection* selection_from_this() { return selection_from(js_get_this()); }

// Reuse cached JS wrapper if the native object already has one; else build.
static Item js_object_for_range(DomRange* r) {
    if (!r) return ItemNull;
    if (r->host_wrapper) return (Item){ .map = (Map*)r->host_wrapper };
    return build_range_object(r);
}
static Item js_object_for_selection(DomSelection* s) {
    if (!s) return ItemNull;
    if (s->host_wrapper) return (Item){ .map = (Map*)s->host_wrapper };
    return build_selection_object(s);
}

// ============================================================================
// Argument helpers
// ============================================================================

static DomNode* node_arg(Item v) {
    void* p = js_dom_unwrap_element(v);
    return (DomNode*)p;
}

// Returns true iff `n` is reachable from the active document — i.e. either
// it is/contains the document stub itself or it has the document.root as an
// ancestor. Used by setBaseAndExtent to mirror the spec's "must be in the
// same tree" abort behaviour (rangeCount stays 0 instead of throwing).
static bool node_in_active_document(DomNode* n) {
    // A NULL node means the boundary hasn't been positioned yet (freshly-created
    // Range defaults to (NULL, 0)). Treat as in-document so that addRange of a
    // brand-new Range still adds it (per spec, the Range belongs to the doc).
    if (!n) return true;
    DomDocument* doc = (DomDocument*)js_dom_get_document();
    if (!doc) return false;
    DomNode* doc_stub = (DomNode*)doc->js_doc_node;
    DomNode* root = (DomNode*)doc->root;
    DomNode* cur = n;
    while (cur) {
        if (cur == doc_stub) return true;
        if (root && cur == root) return true;
        cur = cur->parent;
    }
    return false;
}

// ============================================================================
// Range methods
// ============================================================================

extern "C" Item js_range_set_start(Item node_arg_v, Item offset_arg_v) {
    DomRange* r = range_from_this();
    if (!r) return make_undef();
    DomNode* n = node_arg(node_arg_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_set_start(r, n, (uint32_t)item_to_int(offset_arg_v), &exc)) {
        throw_from_dom_exc(exc, "Range.setStart failed");
        return make_undef();
    }
    range_sync_props(js_get_this(), r);
    return make_undef();
}

extern "C" Item js_range_set_end(Item node_arg_v, Item offset_arg_v) {
    DomRange* r = range_from_this();
    if (!r) return make_undef();
    DomNode* n = node_arg(node_arg_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_set_end(r, n, (uint32_t)item_to_int(offset_arg_v), &exc)) {
        throw_from_dom_exc(exc, "Range.setEnd failed");
        return make_undef();
    }
    range_sync_props(js_get_this(), r);
    return make_undef();
}

extern "C" Item js_range_set_start_before(Item node_v) {
    DomRange* r = range_from_this(); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_set_start_before(r, n, &exc)) {
        throw_from_dom_exc(exc, "Range.setStartBefore failed");
        return make_undef();
    }
    range_sync_props(js_get_this(), r);
    return make_undef();
}

extern "C" Item js_range_set_start_after(Item node_v) {
    DomRange* r = range_from_this(); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_set_start_after(r, n, &exc)) {
        throw_from_dom_exc(exc, "Range.setStartAfter failed");
        return make_undef();
    }
    range_sync_props(js_get_this(), r);
    return make_undef();
}

extern "C" Item js_range_set_end_before(Item node_v) {
    DomRange* r = range_from_this(); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_set_end_before(r, n, &exc)) {
        throw_from_dom_exc(exc, "Range.setEndBefore failed");
        return make_undef();
    }
    range_sync_props(js_get_this(), r);
    return make_undef();
}

extern "C" Item js_range_set_end_after(Item node_v) {
    DomRange* r = range_from_this(); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_set_end_after(r, n, &exc)) {
        throw_from_dom_exc(exc, "Range.setEndAfter failed");
        return make_undef();
    }
    range_sync_props(js_get_this(), r);
    return make_undef();
}

extern "C" Item js_range_collapse(Item to_start_v) {
    DomRange* r = range_from_this(); if (!r) return make_undef();
    bool to_start = false;
    TypeId t = get_type_id(to_start_v);
    if (t != LMD_TYPE_NULL && t != LMD_TYPE_UNDEFINED) {
        to_start = item_to_bool(to_start_v);
    }
    dom_range_collapse(r, to_start);
    range_sync_props(js_get_this(), r);
    return make_undef();
}

extern "C" Item js_range_select_node(Item node_v) {
    DomRange* r = range_from_this(); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_select_node(r, n, &exc)) {
        throw_from_dom_exc(exc, "Range.selectNode failed");
        return make_undef();
    }
    range_sync_props(js_get_this(), r);
    return make_undef();
}

extern "C" Item js_range_select_node_contents(Item node_v) {
    DomRange* r = range_from_this(); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_select_node_contents(r, n, &exc)) {
        throw_from_dom_exc(exc, "Range.selectNodeContents failed");
        return make_undef();
    }
    range_sync_props(js_get_this(), r);
    return make_undef();
}

extern "C" Item js_range_clone_range(void) {
    DomRange* r = range_from_this(); if (!r) return ItemNull;
    DomRange* clone = dom_range_clone(r);
    if (!clone) return ItemNull;
    return build_range_object(clone);
}

extern "C" Item js_range_compare_boundary_points(Item how_v, Item other_v) {
    DomRange* r = range_from_this(); if (!r) return make_int(0);
    int how = item_to_int(how_v);
    DomRange* other = range_from(other_v);
    if (!other) { throw_dom_exception("TypeError", "argument is not a Range"); return make_int(0); }
    const char* exc = nullptr;
    int result = dom_range_compare_boundary_points(
        r, (DomRangeCompareHow)how, other, &exc);
    if (exc) { throw_from_dom_exc(exc, "compareBoundaryPoints failed"); return make_int(0); }
    return make_int(result);
}

extern "C" Item js_range_compare_point(Item node_v, Item offset_v) {
    DomRange* r = range_from_this(); if (!r) return make_int(0);
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_int(0); }
    const char* exc = nullptr;
    int result = dom_range_compare_point(r, n, (uint32_t)item_to_int(offset_v), &exc);
    if (exc) { throw_from_dom_exc(exc, "comparePoint failed"); return make_int(0); }
    return make_int(result);
}

extern "C" Item js_range_is_point_in_range(Item node_v, Item offset_v) {
    DomRange* r = range_from_this(); if (!r) return make_bool(false);
    DomNode* n = node_arg(node_v);
    if (!n) return make_bool(false);
    return make_bool(dom_range_is_point_in_range(r, n, (uint32_t)item_to_int(offset_v)));
}

extern "C" Item js_range_intersects_node(Item node_v) {
    DomRange* r = range_from_this(); if (!r) return make_bool(false);
    DomNode* n = node_arg(node_v);
    if (!n) return make_bool(false);
    return make_bool(dom_range_intersects_node(r, n));
}

extern "C" Item js_range_detach(void) {
    // per spec, detach() is a no-op (legacy)
    return make_undef();
}

extern "C" Item js_range_to_string(void) {
    DomRange* r = range_from_this();
    if (!r) return make_str("");
    char* s = dom_range_to_string(r);
    if (!s) return make_str("");
    Item it = make_str(s);
    free(s);
    return it;
}

// Phase 4 — mutation methods
extern "C" Item js_range_delete_contents(void) {
    DomRange* r = range_from_this(); if (!r) return make_undef();
    const char* exc = nullptr;
    if (!dom_range_delete_contents(r, &exc)) {
        throw_from_dom_exc(exc, "deleteContents failed");
    }
    return make_undef();
}

extern "C" Item js_range_extract_contents(void) {
    DomRange* r = range_from_this(); if (!r) return ItemNull;
    const char* exc = nullptr;
    DomElement* frag = dom_range_extract_contents(r, &exc);
    if (exc && !frag) { throw_from_dom_exc(exc, "extractContents failed"); return ItemNull; }
    return frag ? js_dom_wrap_element(frag) : ItemNull;
}

extern "C" Item js_range_clone_contents(void) {
    DomRange* r = range_from_this(); if (!r) return ItemNull;
    const char* exc = nullptr;
    DomElement* frag = dom_range_clone_contents(r, &exc);
    if (exc && !frag) { throw_from_dom_exc(exc, "cloneContents failed"); return ItemNull; }
    return frag ? js_dom_wrap_element(frag) : ItemNull;
}

extern "C" Item js_range_insert_node(Item node_v) {
    DomRange* r = range_from_this(); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_insert_node(r, n, &exc)) {
        throw_from_dom_exc(exc, "insertNode failed");
    }
    return make_undef();
}

extern "C" Item js_range_surround_contents(Item node_v) {
    DomRange* r = range_from_this(); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_surround_contents(r, n, &exc)) {
        throw_from_dom_exc(exc, "surroundContents failed");
    }
    return make_undef();
}

// ============================================================================
// Range constructor / property dispatch
// ============================================================================

// No-op now — properties are read on demand via js_dom_range_get_property.
// The call sites in mutation methods are kept as documentation cues.
static inline void range_sync_props(Item /*obj*/, DomRange* /*r*/) {}

static Item build_range_object(DomRange* r) {
    if (!r) return ItemNull;
    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id  = LMD_TYPE_MAP;
    m->map_kind = MAP_KIND_DOM;
    m->type     = (void*)&js_dom_range_marker;
    m->data     = r;
    m->data_cap = 0;
    Item obj = (Item){ .map = m };
    r->host_wrapper = m;
    dom_range_retain(r);  // released in js_dom_selection_reset
    return obj;
}

// Cached method Items (lazy-init on first access). Function Items just
// bind a C function pointer + arity, so caching avoids re-allocation.
struct RangeMethods {
    Item setStart, setEnd;
    Item setStartBefore, setStartAfter;
    Item setEndBefore, setEndAfter;
    Item collapse, selectNode, selectNodeContents;
    Item cloneRange, compareBoundaryPoints, comparePoint;
    Item isPointInRange, intersectsNode, detach, toString;
    Item deleteContents, extractContents, cloneContents;
    Item insertNode, surroundContents;
    bool inited;
};
static RangeMethods _range_methods = {};

static void init_range_methods() {
    if (_range_methods.inited) return;
    _range_methods.setStart              = js_new_function((void*)js_range_set_start, 2);
    _range_methods.setEnd                = js_new_function((void*)js_range_set_end, 2);
    _range_methods.setStartBefore        = js_new_function((void*)js_range_set_start_before, 1);
    _range_methods.setStartAfter         = js_new_function((void*)js_range_set_start_after, 1);
    _range_methods.setEndBefore          = js_new_function((void*)js_range_set_end_before, 1);
    _range_methods.setEndAfter           = js_new_function((void*)js_range_set_end_after, 1);
    _range_methods.collapse              = js_new_function((void*)js_range_collapse, 1);
    _range_methods.selectNode            = js_new_function((void*)js_range_select_node, 1);
    _range_methods.selectNodeContents    = js_new_function((void*)js_range_select_node_contents, 1);
    _range_methods.cloneRange            = js_new_function((void*)js_range_clone_range, 0);
    _range_methods.compareBoundaryPoints = js_new_function((void*)js_range_compare_boundary_points, 2);
    _range_methods.comparePoint          = js_new_function((void*)js_range_compare_point, 2);
    _range_methods.isPointInRange        = js_new_function((void*)js_range_is_point_in_range, 2);
    _range_methods.intersectsNode        = js_new_function((void*)js_range_intersects_node, 1);
    _range_methods.detach                = js_new_function((void*)js_range_detach, 0);
    _range_methods.toString              = js_new_function((void*)js_range_to_string, 0);
    _range_methods.deleteContents        = js_new_function((void*)js_range_delete_contents, 0);
    _range_methods.extractContents       = js_new_function((void*)js_range_extract_contents, 0);
    _range_methods.cloneContents         = js_new_function((void*)js_range_clone_contents, 0);
    _range_methods.insertNode            = js_new_function((void*)js_range_insert_node, 1);
    _range_methods.surroundContents      = js_new_function((void*)js_range_surround_contents, 1);
    _range_methods.inited = true;
}

extern "C" Item js_dom_range_get_property(Item obj, Item key) {
    DomRange* r = range_from(obj);
    if (!r) return ItemNull;
    const char* p = fn_to_cstr(key);
    if (!p) return ItemNull;

    // Live data attributes
    if (strcmp(p, "startContainer") == 0)
        return r->start.node ? js_dom_wrap_element(r->start.node) : ItemNull;
    if (strcmp(p, "startOffset") == 0)
        return make_int((int64_t)r->start.offset);
    if (strcmp(p, "endContainer") == 0)
        return r->end.node ? js_dom_wrap_element(r->end.node) : ItemNull;
    if (strcmp(p, "endOffset") == 0)
        return make_int((int64_t)r->end.offset);
    if (strcmp(p, "collapsed") == 0)
        return make_bool(dom_range_collapsed(r));
    if (strcmp(p, "commonAncestorContainer") == 0) {
        DomNode* anc = dom_range_common_ancestor(r);
        return anc ? js_dom_wrap_element(anc) : ItemNull;
    }
    // Range constants
    if (strcmp(p, "START_TO_START") == 0) return make_int(0);
    if (strcmp(p, "START_TO_END")   == 0) return make_int(1);
    if (strcmp(p, "END_TO_END")     == 0) return make_int(2);
    if (strcmp(p, "END_TO_START")   == 0) return make_int(3);

    // Methods (cached)
    init_range_methods();
    if (strcmp(p, "setStart") == 0)              return _range_methods.setStart;
    if (strcmp(p, "setEnd") == 0)                return _range_methods.setEnd;
    if (strcmp(p, "setStartBefore") == 0)        return _range_methods.setStartBefore;
    if (strcmp(p, "setStartAfter") == 0)         return _range_methods.setStartAfter;
    if (strcmp(p, "setEndBefore") == 0)          return _range_methods.setEndBefore;
    if (strcmp(p, "setEndAfter") == 0)           return _range_methods.setEndAfter;
    if (strcmp(p, "collapse") == 0)              return _range_methods.collapse;
    if (strcmp(p, "selectNode") == 0)            return _range_methods.selectNode;
    if (strcmp(p, "selectNodeContents") == 0)    return _range_methods.selectNodeContents;
    if (strcmp(p, "cloneRange") == 0)            return _range_methods.cloneRange;
    if (strcmp(p, "compareBoundaryPoints") == 0) return _range_methods.compareBoundaryPoints;
    if (strcmp(p, "comparePoint") == 0)          return _range_methods.comparePoint;
    if (strcmp(p, "isPointInRange") == 0)        return _range_methods.isPointInRange;
    if (strcmp(p, "intersectsNode") == 0)        return _range_methods.intersectsNode;
    if (strcmp(p, "detach") == 0)                return _range_methods.detach;
    if (strcmp(p, "toString") == 0)              return _range_methods.toString;
    if (strcmp(p, "deleteContents") == 0)        return _range_methods.deleteContents;
    if (strcmp(p, "extractContents") == 0)       return _range_methods.extractContents;
    if (strcmp(p, "cloneContents") == 0)         return _range_methods.cloneContents;
    if (strcmp(p, "insertNode") == 0)            return _range_methods.insertNode;
    if (strcmp(p, "surroundContents") == 0)      return _range_methods.surroundContents;

    return ItemNull;
}

// ============================================================================
// Selection methods
// ============================================================================

extern "C" Item js_selection_get_range_at(Item index_v) {
    DomSelection* s = selection_from_this();
    if (!s) return ItemNull;
    const char* exc = nullptr;
    DomRange* r = dom_selection_get_range_at(s, (uint32_t)item_to_int(index_v), &exc);
    if (!r) {
        throw_from_dom_exc(exc ? exc : "IndexSizeError",
                           "Selection.getRangeAt: bad index");
        return ItemNull;
    }
    return js_object_for_range(r);
}

extern "C" Item js_selection_add_range(Item range_v) {
    DomSelection* s = selection_from_this(); if (!s) return make_undef();
    DomRange* r = range_from(range_v);
    if (!r) { throw_dom_exception("TypeError", "argument is not a Range"); return make_undef(); }
    // Per spec: if range's root is not the document associated with this,
    // return (do nothing). The WPT tests assert rangeCount is unchanged.
    if (!node_in_active_document(r->start.node) ||
        !node_in_active_document(r->end.node)) {
        return make_undef();
    }
    dom_selection_add_range(s, r);
    selection_sync_props(js_get_this(), s);
    return make_undef();
}

extern "C" Item js_selection_remove_range(Item range_v) {
    DomSelection* s = selection_from_this(); if (!s) return make_undef();
    DomRange* r = range_from(range_v);
    if (!r) { throw_dom_exception("NotFoundError", "range not in selection"); return make_undef(); }
    // Per WHATWG: throw NotFoundError if the range isn't in the selection.
    bool in_sel = false;
    uint32_t rc = dom_selection_range_count(s);
    for (uint32_t i = 0; i < rc; i++) {
        const char* exc = nullptr;
        if (dom_selection_get_range_at(s, i, &exc) == r) { in_sel = true; break; }
    }
    if (!in_sel) { throw_dom_exception("NotFoundError", "range not in selection"); return make_undef(); }
    dom_selection_remove_range(s, r);
    selection_sync_props(js_get_this(), s);
    return make_undef();
}

extern "C" Item js_selection_remove_all_ranges(void) {
    DomSelection* s = selection_from_this(); if (!s) return make_undef();
    dom_selection_remove_all_ranges(s);
    selection_sync_props(js_get_this(), s);
    return make_undef();
}

extern "C" Item js_selection_empty(void) {
    DomSelection* s = selection_from_this(); if (!s) return make_undef();
    dom_selection_empty(s);
    selection_sync_props(js_get_this(), s);
    return make_undef();
}

extern "C" Item js_selection_collapse(Item node_v, Item offset_v) {
    DomSelection* s = selection_from_this(); if (!s) return make_undef();
    DomNode* n = node_arg(node_v);
    // per spec: collapse(null) clears the selection
    if (!n && get_type_id(node_v) == LMD_TYPE_NULL) {
        dom_selection_remove_all_ranges(s);
        selection_sync_props(js_get_this(), s);
        return make_undef();
    }
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_selection_collapse(s, n, (uint32_t)item_to_int(offset_v), &exc)) {
        throw_from_dom_exc(exc, "Selection.collapse failed");
        return make_undef();
    }
    selection_sync_props(js_get_this(), s);
    return make_undef();
}

extern "C" Item js_selection_set_position(Item node_v, Item offset_v) {
    return js_selection_collapse(node_v, offset_v);
}

extern "C" Item js_selection_collapse_to_start(void) {
    DomSelection* s = selection_from_this(); if (!s) return make_undef();
    const char* exc = nullptr;
    dom_selection_collapse_to_start(s, &exc);
    if (exc) { throw_from_dom_exc(exc, "collapseToStart failed"); return make_undef(); }
    selection_sync_props(js_get_this(), s);
    return make_undef();
}

extern "C" Item js_selection_collapse_to_end(void) {
    DomSelection* s = selection_from_this(); if (!s) return make_undef();
    const char* exc = nullptr;
    dom_selection_collapse_to_end(s, &exc);
    if (exc) { throw_from_dom_exc(exc, "collapseToEnd failed"); return make_undef(); }
    selection_sync_props(js_get_this(), s);
    return make_undef();
}

extern "C" Item js_selection_extend(Item node_v, Item offset_v) {
    DomSelection* s = selection_from_this(); if (!s) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    // Per spec: if node's root is not the document, abort (no-op).
    if (!node_in_active_document(n)) {
        return make_undef();
    }
    const char* exc = nullptr;
    if (!dom_selection_extend(s, n, (uint32_t)item_to_int(offset_v), &exc)) {
        throw_from_dom_exc(exc, "Selection.extend failed");
        return make_undef();
    }
    selection_sync_props(js_get_this(), s);
    return make_undef();
}

extern "C" Item js_selection_set_base_and_extent(Item anchor_node_v, Item anchor_off_v,
                                                  Item focus_node_v,  Item focus_off_v) {
    DomSelection* s = selection_from_this(); if (!s) return make_undef();
    // Per WebIDL, all four arguments are required: missing args (passed as
    // undefined) → TypeError.
    TypeId ta = get_type_id(anchor_off_v);
    TypeId tf = get_type_id(focus_off_v);
    if (ta == LMD_TYPE_UNDEFINED || tf == LMD_TYPE_UNDEFINED ||
        get_type_id(focus_node_v) == LMD_TYPE_UNDEFINED) {
        throw_dom_exception("TypeError", "setBaseAndExtent: 4 arguments required");
        return make_undef();
    }
    DomNode* an = node_arg(anchor_node_v);
    DomNode* fn = node_arg(focus_node_v);
    if (!an || !fn) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    // Per spec: if either node isn't a descendant of the document, abort
    // (selection is left empty — do not add a range).
    if (!node_in_active_document(an) || !node_in_active_document(fn)) {
        dom_selection_remove_all_ranges(s);
        selection_sync_props(js_get_this(), s);
        return make_undef();
    }
    const char* exc = nullptr;
    if (!dom_selection_set_base_and_extent(s, an, (uint32_t)item_to_int(anchor_off_v),
                                              fn, (uint32_t)item_to_int(focus_off_v), &exc)) {
        throw_from_dom_exc(exc, "setBaseAndExtent failed");
        return make_undef();
    }
    selection_sync_props(js_get_this(), s);
    return make_undef();
}

extern "C" Item js_selection_select_all_children(Item node_v) {
    DomSelection* s = selection_from_this(); if (!s) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_selection_select_all_children(s, n, &exc)) {
        throw_from_dom_exc(exc, "selectAllChildren failed");
        return make_undef();
    }
    selection_sync_props(js_get_this(), s);
    return make_undef();
}

extern "C" Item js_selection_contains_node(Item node_v, Item allow_partial_v) {
    DomSelection* s = selection_from_this(); if (!s) return make_bool(false);
    DomNode* n = node_arg(node_v);
    if (!n) return make_bool(false);
    bool partial = false;
    TypeId t = get_type_id(allow_partial_v);
    if (t != LMD_TYPE_NULL && t != LMD_TYPE_UNDEFINED) partial = item_to_bool(allow_partial_v);
    return make_bool(dom_selection_contains_node(s, n, partial));
}

extern "C" Item js_selection_delete_from_document(void) {
    DomSelection* s = selection_from_this(); if (!s) return make_undef();
    dom_selection_delete_from_document(s);
    selection_sync_props(js_get_this(), s);
    return make_undef();
}

extern "C" Item js_selection_to_string(void) {
    DomSelection* s = selection_from_this();
    if (!s) return make_str("");
    if (dom_selection_range_count(s) == 0) return make_str("");
    DomRange* r = dom_selection_get_range_at(s, 0, nullptr);
    if (!r) return make_str("");
    char* out = dom_range_to_string(r);
    if (!out) return make_str("");
    Item it = make_str(out);
    free(out);
    return it;
}

extern "C" Item js_selection_modify(Item alter_v, Item dir_v, Item gran_v) {
    DomSelection* s = selection_from_this(); if (!s) return make_undef();
    const char* alter = fn_to_cstr(alter_v);
    const char* dir   = fn_to_cstr(dir_v);
    const char* gran  = fn_to_cstr(gran_v);
    const char* exc = nullptr;
    if (!dom_selection_modify(s, alter, dir, gran, &exc)) {
        throw_from_dom_exc(exc, "Selection.modify failed");
        return make_undef();
    }
    selection_sync_props(js_get_this(), s);
    return make_undef();
}

// ============================================================================
// Selection construction / property sync
// ============================================================================

static inline void selection_sync_props(Item /*obj*/, DomSelection* /*s*/) {}

static Item build_selection_object(DomSelection* s) {
    if (!s) return ItemNull;
    Map* m = (Map*)heap_calloc(sizeof(Map), LMD_TYPE_MAP);
    m->type_id  = LMD_TYPE_MAP;
    m->map_kind = MAP_KIND_DOM;
    m->type     = (void*)&js_dom_selection_marker;
    m->data     = s;
    m->data_cap = 0;
    Item obj = (Item){ .map = m };
    s->host_wrapper = m;
    return obj;
}

// Cached method Items
struct SelectionMethods {
    Item getRangeAt, addRange, removeRange, removeAllRanges;
    Item empty, collapse, setPosition, collapseToStart, collapseToEnd;
    Item extend, setBaseAndExtent, selectAllChildren, containsNode;
    Item deleteFromDocument, toString, modify;
    bool inited;
};
static SelectionMethods _sel_methods = {};

static void init_selection_methods() {
    if (_sel_methods.inited) return;
    _sel_methods.getRangeAt         = js_new_function((void*)js_selection_get_range_at, 1);
    _sel_methods.addRange           = js_new_function((void*)js_selection_add_range, 1);
    _sel_methods.removeRange        = js_new_function((void*)js_selection_remove_range, 1);
    _sel_methods.removeAllRanges    = js_new_function((void*)js_selection_remove_all_ranges, 0);
    _sel_methods.empty              = js_new_function((void*)js_selection_empty, 0);
    _sel_methods.collapse           = js_new_function((void*)js_selection_collapse, 2);
    _sel_methods.setPosition        = js_new_function((void*)js_selection_set_position, 2);
    _sel_methods.collapseToStart    = js_new_function((void*)js_selection_collapse_to_start, 0);
    _sel_methods.collapseToEnd      = js_new_function((void*)js_selection_collapse_to_end, 0);
    _sel_methods.extend             = js_new_function((void*)js_selection_extend, 2);
    _sel_methods.setBaseAndExtent   = js_new_function((void*)js_selection_set_base_and_extent, 4);
    _sel_methods.selectAllChildren  = js_new_function((void*)js_selection_select_all_children, 1);
    _sel_methods.containsNode       = js_new_function((void*)js_selection_contains_node, 2);
    _sel_methods.deleteFromDocument = js_new_function((void*)js_selection_delete_from_document, 0);
    _sel_methods.toString           = js_new_function((void*)js_selection_to_string, 0);
    _sel_methods.modify             = js_new_function((void*)js_selection_modify, 3);
    _sel_methods.inited = true;
}

extern "C" Item js_dom_selection_get_property(Item obj, Item key) {
    DomSelection* s = selection_from(obj);
    if (!s) return ItemNull;
    const char* p = fn_to_cstr(key);
    if (!p) return ItemNull;

    // Live data attributes
    if (strcmp(p, "anchorNode") == 0) {
        DomNode* n = dom_selection_anchor_node(s);
        return n ? js_dom_wrap_element(n) : ItemNull;
    }
    if (strcmp(p, "anchorOffset") == 0)
        return make_int((int64_t)dom_selection_anchor_offset(s));
    if (strcmp(p, "focusNode") == 0) {
        DomNode* n = dom_selection_focus_node(s);
        return n ? js_dom_wrap_element(n) : ItemNull;
    }
    if (strcmp(p, "focusOffset") == 0)
        return make_int((int64_t)dom_selection_focus_offset(s));
    if (strcmp(p, "isCollapsed") == 0)
        return make_bool(dom_selection_is_collapsed(s));
    if (strcmp(p, "rangeCount") == 0)
        return make_int((int64_t)dom_selection_range_count(s));
    if (strcmp(p, "type") == 0)
        return make_str(dom_selection_type(s));

    init_selection_methods();
    if (strcmp(p, "getRangeAt") == 0)         return _sel_methods.getRangeAt;
    if (strcmp(p, "addRange") == 0)           return _sel_methods.addRange;
    if (strcmp(p, "removeRange") == 0)        return _sel_methods.removeRange;
    if (strcmp(p, "removeAllRanges") == 0)    return _sel_methods.removeAllRanges;
    if (strcmp(p, "empty") == 0)              return _sel_methods.empty;
    if (strcmp(p, "collapse") == 0)           return _sel_methods.collapse;
    if (strcmp(p, "setPosition") == 0)        return _sel_methods.setPosition;
    if (strcmp(p, "collapseToStart") == 0)    return _sel_methods.collapseToStart;
    if (strcmp(p, "collapseToEnd") == 0)      return _sel_methods.collapseToEnd;
    if (strcmp(p, "extend") == 0)             return _sel_methods.extend;
    if (strcmp(p, "setBaseAndExtent") == 0)   return _sel_methods.setBaseAndExtent;
    if (strcmp(p, "selectAllChildren") == 0)  return _sel_methods.selectAllChildren;
    if (strcmp(p, "containsNode") == 0)       return _sel_methods.containsNode;
    if (strcmp(p, "deleteFromDocument") == 0) return _sel_methods.deleteFromDocument;
    if (strcmp(p, "toString") == 0)           return _sel_methods.toString;
    if (strcmp(p, "modify") == 0)             return _sel_methods.modify;

    return ItemNull;
}

// ============================================================================
// Public entry points
// ============================================================================

extern "C" Item js_dom_create_range(void) {
    RadiantState* state = get_or_create_state();
    if (!state) {
        log_error("createRange: no document state");
        return ItemNull;
    }
    DomRange* r = dom_range_create(state);
    if (!r) return ItemNull;
    return build_range_object(r);
}

extern "C" Item js_dom_get_selection(void) {
    RadiantState* state = get_or_create_state();
    if (!state) {
        log_error("getSelection: no document state");
        return ItemNull;
    }
    if (!state->dom_selection) {
        state->dom_selection = dom_selection_create(state);
    }
    return js_object_for_selection(state->dom_selection);
}

extern "C" Item js_dom_wrap_range(void* dom_range) {
    return js_object_for_range((DomRange*)dom_range);
}

extern "C" bool js_dom_is_range_object(Item item) {
    return js_dom_item_is_range(item);
}

extern "C" bool js_dom_is_selection_object(Item item) {
    return js_dom_item_is_selection(item);
}

extern "C" void* js_dom_unwrap_range(Item item) {
    return (void*)range_from(item);
}

extern "C" void* js_dom_unwrap_selection(Item item) {
    return (void*)selection_from(item);
}

// ============================================================================
// Global installation
// ============================================================================

// Trampoline so we can register getSelection as a global function with arity 0.
extern "C" Item js_global_get_selection(void) {
    return js_dom_get_selection();
}

extern "C" void js_dom_selection_install_globals(void) {
    Item global = js_get_global_this();
    Item fn = js_new_function((void*)js_global_get_selection, 0);
    js_property_set(global, make_key("getSelection"), fn);
    // Ensure `window` resolves to globalThis so `window.getSelection()` works.
    Item window_key = make_key("window");
    Item existing = js_property_get(global, window_key);
    if (get_type_id(existing) != LMD_TYPE_MAP) {
        js_property_set(global, window_key, global);
    } else {
        // window is already a real object — install getSelection on it too
        js_property_set(existing, make_key("getSelection"), fn);
    }

    // Install placeholder Selection / Range constructors so `instanceof Selection`
    // and feature-detection (`window.Selection`) succeed. The constructors are
    // never actually invoked by typical WPT code (which uses document.createRange
    // / getSelection); they exist purely as identity markers.
    Item sel_ctor   = js_new_function((void*)js_global_get_selection, 0);
    Item range_ctor = js_new_function((void*)js_dom_create_range, 0);
    js_property_set(global, make_key("Selection"), sel_ctor);
    js_property_set(global, make_key("Range"),     range_ctor);

    // Install Selection.prototype and Range.prototype with method stubs so
    // WPT idl checks like `Selection.prototype.deleteFromDocument.length`
    // succeed. The methods themselves are never invoked through the
    // prototype path (instances are MAP_KIND_DOM and dispatch through their
    // own get_property hooks); these are pure idl shape.
    init_selection_methods();
    init_range_methods();
    Item sel_proto = js_property_get(sel_ctor, make_key("prototype"));
    if (get_type_id(sel_proto) != LMD_TYPE_MAP) {
        sel_proto = js_property_get(sel_ctor, make_key("prototype"));
    }
    // Above may still be undefined if the function has no auto-prototype.
    // Force-create one via property_set fallback path.
    if (get_type_id(sel_proto) != LMD_TYPE_MAP) {
        extern Item js_new_object();
        sel_proto = js_new_object();
        js_property_set(sel_ctor, make_key("prototype"), sel_proto);
    }
    js_property_set(sel_proto, make_key("getRangeAt"),         _sel_methods.getRangeAt);
    js_property_set(sel_proto, make_key("addRange"),           _sel_methods.addRange);
    js_property_set(sel_proto, make_key("removeRange"),        _sel_methods.removeRange);
    js_property_set(sel_proto, make_key("removeAllRanges"),    _sel_methods.removeAllRanges);
    js_property_set(sel_proto, make_key("empty"),              _sel_methods.empty);
    js_property_set(sel_proto, make_key("collapse"),           _sel_methods.collapse);
    js_property_set(sel_proto, make_key("setPosition"),        _sel_methods.setPosition);
    js_property_set(sel_proto, make_key("collapseToStart"),    _sel_methods.collapseToStart);
    js_property_set(sel_proto, make_key("collapseToEnd"),      _sel_methods.collapseToEnd);
    js_property_set(sel_proto, make_key("extend"),             _sel_methods.extend);
    js_property_set(sel_proto, make_key("setBaseAndExtent"),   _sel_methods.setBaseAndExtent);
    js_property_set(sel_proto, make_key("selectAllChildren"),  _sel_methods.selectAllChildren);
    js_property_set(sel_proto, make_key("containsNode"),       _sel_methods.containsNode);
    js_property_set(sel_proto, make_key("deleteFromDocument"), _sel_methods.deleteFromDocument);
    js_property_set(sel_proto, make_key("toString"),           _sel_methods.toString);
    js_property_set(sel_proto, make_key("modify"),             _sel_methods.modify);

    Item range_proto = js_property_get(range_ctor, make_key("prototype"));
    if (get_type_id(range_proto) != LMD_TYPE_MAP) {
        extern Item js_new_object();
        range_proto = js_new_object();
        js_property_set(range_ctor, make_key("prototype"), range_proto);
    }
    js_property_set(range_proto, make_key("setStart"),              _range_methods.setStart);
    js_property_set(range_proto, make_key("setEnd"),                _range_methods.setEnd);
    js_property_set(range_proto, make_key("setStartBefore"),        _range_methods.setStartBefore);
    js_property_set(range_proto, make_key("setStartAfter"),         _range_methods.setStartAfter);
    js_property_set(range_proto, make_key("setEndBefore"),          _range_methods.setEndBefore);
    js_property_set(range_proto, make_key("setEndAfter"),           _range_methods.setEndAfter);
    js_property_set(range_proto, make_key("collapse"),              _range_methods.collapse);
    js_property_set(range_proto, make_key("selectNode"),            _range_methods.selectNode);
    js_property_set(range_proto, make_key("selectNodeContents"),    _range_methods.selectNodeContents);
    js_property_set(range_proto, make_key("cloneRange"),            _range_methods.cloneRange);
    js_property_set(range_proto, make_key("compareBoundaryPoints"), _range_methods.compareBoundaryPoints);
    js_property_set(range_proto, make_key("comparePoint"),          _range_methods.comparePoint);
    js_property_set(range_proto, make_key("isPointInRange"),        _range_methods.isPointInRange);
    js_property_set(range_proto, make_key("intersectsNode"),        _range_methods.intersectsNode);
    js_property_set(range_proto, make_key("detach"),                _range_methods.detach);
    js_property_set(range_proto, make_key("toString"),              _range_methods.toString);
    js_property_set(range_proto, make_key("deleteContents"),        _range_methods.deleteContents);
    js_property_set(range_proto, make_key("extractContents"),       _range_methods.extractContents);
    js_property_set(range_proto, make_key("cloneContents"),         _range_methods.cloneContents);
    js_property_set(range_proto, make_key("insertNode"),            _range_methods.insertNode);
    js_property_set(range_proto, make_key("surroundContents"),      _range_methods.surroundContents);

    // Set document.defaultView = window so DOM tests' sanity checks pass.
    Item doc = js_get_document_object_value();
    js_document_proxy_set_property(make_key("defaultView"), global);
    (void)doc;

    log_debug("js_dom_selection: installed global getSelection / Selection / Range");
}

extern "C" void js_dom_selection_reset(void) {
    // Native lifetime is owned by the per-document RadiantState. Walk the
    // current document's live ranges, drop our JS-side retain, and clear
    // host_wrapper back-pointers so a fresh document starts clean.
    DomDocument* doc = (DomDocument*)js_dom_get_document();
    if (!doc || !doc->state) return;
    RadiantState* state = doc->state;
    DomRange* r = state->live_ranges;
    while (r) {
        DomRange* next = r->next;
        r->host_wrapper = nullptr;
        dom_range_release(r);  // pairs with retain in build_range_object
        r = next;
    }
    if (state->dom_selection) state->dom_selection->host_wrapper = nullptr;
}
