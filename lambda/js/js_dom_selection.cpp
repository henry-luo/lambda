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
 * The selection / live-range list lives on the document's StateStore-backed
 * DocState projection. In headless JS mode that may be NULL on entry, so
 * we lazily create the document StateStore on first use.
 */

#include "js_dom_selection.h"
#include "js_dom.h"
#include "js_dom_events.h"
#include "js_event_loop.h"
#include "js_props.h"
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "../lambda.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../transpiler.hpp"
#include "../jube/jube_registry.h"
#include "../jube/jube_interface.h"
#include "../input/css/dom_node.hpp"
#include "../input/css/dom_element.hpp"
#include "../../lib/arraylist.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"

#include "../../radiant/event.hpp"
#include "../../radiant/view.hpp"
#include "../../radiant/event.hpp"
#include <cstring>
#include <cstdlib>
#include <cstdio>

extern __thread EvalContext* context;
extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);
extern "C" void* js_dom_current_active_text_control(void);
extern "C" bool js_doc_has_browsing_context(void* doc);
extern "C" Item js_object_get_own_property_descriptor(Item obj, Item name);
extern "C" Item js_object_get_own_property_names(Item object);
extern "C" Item js_prototype_lookup_ex(Item object, Item property, bool* out_found);
extern "C" Item js_get_prototype(Item object);

static Item js_dom_flush_selectionchange(Item this_val, Item* args, int argc);

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

static inline Item make_number_from_float(float v) {
    return (Item){.item = i2it((int64_t)v)};
}

static inline Item make_str(const char* s) {
    if (!s) return ItemNull;
    return (Item){.item = s2it(heap_create_name(s))};
}

static inline Item make_key(const char* s) { return make_str(s); }

struct JsDocRuntimeScope {
    EvalContext runtime_ctx;
    EvalContext* saved_context;
    ArrayList* type_list;
    bool active;
};

static bool js_doc_runtime_enter_if_needed(DomDocument* doc, JsDocRuntimeScope* scope) {
    if (!scope) return false;
    memset(scope, 0, sizeof(JsDocRuntimeScope));
    if (!doc) return false;
    if (context) {
        js_dom_set_document(doc);
        return true;
    }
    if (!doc->js_runtime_heap || !doc->js_runtime_nursery || !doc->js_runtime_name_pool) return false;
    scope->runtime_ctx.heap = (Heap*)doc->js_runtime_heap;
    scope->runtime_ctx.nursery = (gc_nursery_t*)doc->js_runtime_nursery;
    scope->runtime_ctx.name_pool = (NamePool*)doc->js_runtime_name_pool;
    scope->runtime_ctx.pool = doc->js_runtime_pool ?
        (Pool*)doc->js_runtime_pool : scope->runtime_ctx.heap->pool;
    scope->type_list = arraylist_new(16);
    scope->runtime_ctx.type_list = scope->type_list;
    scope->saved_context = context;
    context = &scope->runtime_ctx;
    js_dom_set_document(doc);
    scope->active = true;
    return true;
}

static void js_doc_runtime_exit(JsDocRuntimeScope* scope) {
    if (!scope || !scope->active) return;
    context = scope->saved_context;
    if (scope->type_list) {
        arraylist_free(scope->type_list);
        scope->type_list = nullptr;
    }
    scope->active = false;
}

static int item_to_int(Item v) {
    TypeId t = get_type_id(v);
    if (t == LMD_TYPE_INT) return (int)it2i(v);
    // JIT offsets can be inline floats or boxed wide numerics; the canonical
    // conversion respects both representations instead of forging a pointer.
    if (t == LMD_TYPE_INT64 || t == LMD_TYPE_FLOAT || t == LMD_TYPE_FLOAT64)
        return (int)it2d(v);
    return 0;
}

static uint32_t selection_text_offset_from_item(DomNode* node, Item value) {
    int offset = item_to_int(value);
    if (!node || !node->is_text()) {
        return offset < 0 ? 0 : (uint32_t)offset;
    }
    uint32_t length = dom_node_boundary_length(node);
    if (offset < 0) return 0;
    if ((uint32_t)offset > length) return length;
    return (uint32_t)offset;
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
// Per-document DocState (lazy in headless JS mode)
// ============================================================================

static DocState* get_or_create_state() {
    DomDocument* doc = (DomDocument*)js_dom_get_document();
    if (!doc) return nullptr;
    if (doc->state) return doc->state;
    if (!doc->pool) return nullptr;
    return radiant_document_ensure_state(doc, "js_dom_selection");
}

// ============================================================================
// Range / Selection wrappers — native host VMaps carry the native pointer.
// Property reads/writes still route through js_dom_get_property /
// js_dom_set_property, which dispatch by wrapper brand into
// js_dom_range_get_property / js_dom_selection_get_property.
// No JS-visible "private" properties; no shape allocations on the wrapper.
// ============================================================================

extern "C" Item vmap_new(void);

static const char js_dom_range_vmap_type_marker = 0;
static const char js_dom_selection_vmap_type_marker = 0;
extern "C" const void* radiant_dom_range_host_type(void);
extern "C" const void* radiant_dom_selection_host_type(void);

// Legacy map marker addresses — kept so older resource-map callers fail closed
// through the same unwrap helpers during this migration window.
TypeMap js_dom_range_marker     = {};
TypeMap js_dom_selection_marker = {};

// Forward decls
static Item build_range_object(DomRange* r);
static Item build_selection_object(DomSelection* s);
static inline void range_sync_props(Item, DomRange*);
static inline void selection_sync_props(Item, DomSelection*);
extern "C" Item js_dom_range_to_string_value(Item obj);
extern "C" Item js_dom_selection_to_string_value(Item obj);

// Identity / unwrap ----------------------------------------------------------

extern "C" bool js_dom_item_is_range(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_VMAP) {
        return item.vmap &&
            (item.vmap->host_type == (const void*)&js_dom_range_vmap_type_marker ||
             item.vmap->host_type == radiant_dom_range_host_type()) &&
            item.vmap->host_data != nullptr;
    }
    if (type != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m && m->map_kind == MAP_KIND_WEB_API_RESOURCE &&
           m->type == (void*)&js_dom_range_marker;
}

extern "C" bool js_dom_item_is_selection(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_VMAP) {
        return item.vmap &&
            (item.vmap->host_type == (const void*)&js_dom_selection_vmap_type_marker ||
             item.vmap->host_type == radiant_dom_selection_host_type()) &&
            item.vmap->host_data != nullptr;
    }
    if (type != LMD_TYPE_MAP) return false;
    Map* m = item.map;
    return m && m->map_kind == MAP_KIND_WEB_API_RESOURCE &&
           m->type == (void*)&js_dom_selection_marker;
}

static inline DomRange* range_from(Item obj) {
    if (!js_dom_item_is_range(obj)) return nullptr;
    if (get_type_id(obj) == LMD_TYPE_VMAP) return (DomRange*)obj.vmap->host_data;
    return (DomRange*)obj.map->data;
}
static inline DomSelection* selection_from(Item obj) {
    if (!js_dom_item_is_selection(obj)) return nullptr;
    if (get_type_id(obj) == LMD_TYPE_VMAP) return (DomSelection*)obj.vmap->host_data;
    return (DomSelection*)obj.map->data;
}

static bool selection_state_set(DomSelection* s,
                                const DomBoundary* anchor,
                                const DomBoundary* focus,
                                const char** out_exception) {
    if (!s || !s->state) {
        if (out_exception) *out_exception = "InvalidStateError";
        return false;
    }
    return state_store_set_selection(s->state, anchor, focus, out_exception);
}

static bool selection_state_clear(DomSelection* s,
                                  const char** out_exception) {
    return selection_state_set(s, nullptr, nullptr, out_exception);
}

static Item get_dom_constructor_prototype(const char* ctor_name) {
    Item global = js_get_global_this();
    Item ctor = js_property_get(global, make_key(ctor_name));
    if (get_type_id(ctor) != LMD_TYPE_FUNC) return ItemNull;
    Item proto = js_property_get(ctor, make_key("prototype"));
    return get_type_id(proto) == LMD_TYPE_MAP ? proto : ItemNull;
}

extern "C" Item js_dom_range_get_prototype_value(void) {
    return get_dom_constructor_prototype("Range");
}

extern "C" Item js_dom_selection_get_prototype_value(void) {
    return get_dom_constructor_prototype("Selection");
}

// Reuse cached JS wrapper if the native object already has one; else build.
static Item js_object_for_range(DomRange* r) {
    if (!r) return ItemNull;
    if (r->host_wrapper) return (Item){ .vmap = (VMap*)r->host_wrapper };
    return build_range_object(r);
}
static Item js_object_for_selection(DomSelection* s) {
    if (!s) return ItemNull;
    if (s->host_wrapper) return (Item){ .vmap = (VMap*)s->host_wrapper };
    return build_selection_object(s);
}

// ============================================================================
// Argument helpers
// ============================================================================

static DomNode* node_arg(Item v) {
    TypeId type = get_type_id(v);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) return nullptr;
    void* p = js_dom_unwrap_element(v);
    if (!p) {
        Item doc_obj = js_get_document_object_value();
        if (v.item == doc_obj.item) {
            p = js_dom_unwrap_element(doc_obj);
        }
    }
    if (!p) {
        Item node_type = js_property_access(v, make_key("nodeType"));
        if (item_to_int(node_type) == 9) {
            p = js_dom_unwrap_element(js_get_document_object_value());
        }
    }
    return (DomNode*)p;
}

static DomElement* active_text_control_for_selection(DomSelection* s) {
    if (!s || !s->state) return nullptr;
    DocState* state = s->state;
    if (state->sel.kind == EDIT_SEL_TEXT_CONTROL &&
        state->sel.control && tc_is_text_control(state->sel.control)) {
        return state->sel.control;
    }
    DomElement* control = tc_get_active_element(state);
    if (control && tc_is_text_control(control)) return control;
    control = tc_get_last_focused_text_control(state);
    if (control && tc_is_text_control(control)) return control;
    View* focused = focus_get(state);
    if (focused && focused->is_element()) {
        DomElement* elem = focused->as_element();
        if (elem && tc_is_text_control(elem)) return elem;
    }
    DomElement* current = (DomElement*)js_dom_current_active_text_control();
    if (current && tc_is_text_control(current)) {
        DocState* current_state = current->doc ? (DocState*)current->doc->state : nullptr;
        if (!current_state || current_state == state) return current;
    }
    return nullptr;
}

static DocState* state_for_text_control_selection(DomSelection* s,
                                                  DomElement* control) {
    DocState* state = control && control->doc ?
        (DocState*)control->doc->state : nullptr;
    return state ? state : (s ? s->state : nullptr);
}

// Resolve the DomDocument owning `n` by walking up to the nearest DomElement.
static DomDocument* node_owning_doc(DomNode* n) {
    while (n) {
        if (n->is_element()) return n->as_element()->doc;
        n = n->parent;
    }
    return nullptr;
}

static bool node_is_document_fragment(DomNode* n) {
    if (!n || !n->is_element()) return false;
    DomElement* elem = n->as_element();
    return elem && elem->tag_name &&
        strcmp(elem->tag_name, "#document-fragment") == 0;
}

// Returns true iff `n` is reachable from the selection's associated document.
// Shadow roots are parentless document fragments in our native DOM, so walk
// through their native host pointer before applying the document-root test.
// Plain detached DocumentFragments still abort, matching Selection's "same
// tree" behavior.
//
// Important: the selection's document may NOT be the thread-local active
// document — e.g. when JS holds a reference to an iframe's
// contentWindow.getSelection() and calls addRange after the active doc has
// been restored to the parent doc. We resolve the selection's doc via its
// `state` field (each DomDocument owns one DocState).
static bool node_in_selection_doc(DomNode* n, DomSelection* s) {
    // A NULL node means the boundary hasn't been positioned yet (freshly-created
    // Range defaults to (NULL, 0)). Treat as in-document so that addRange of a
    // brand-new Range still adds it (per spec, the Range belongs to the doc).
    if (!n) return true;
    DomDocument* sel_doc = nullptr;
    if (s && s->state) {
        // Walk node up to find its doc, then prefer that as the selection's
        // doc when the doc's state matches the selection's state.
        DomDocument* nd = node_owning_doc(n);
        if (nd && nd->state == s->state) sel_doc = nd;
    }
    if (!sel_doc) sel_doc = (DomDocument*)js_dom_get_document();
    if (!sel_doc) return false;
    DomNode* doc_stub = (DomNode*)sel_doc->js_doc_node;
    DomNode* root = (DomNode*)sel_doc->root;
    DomNode* cur = n;
    while (cur) {
        if (node_is_document_fragment(cur)) {
            DomElement* frag = cur->as_element();
            if (frag && frag->shadow_host && frag->doc == sel_doc) {
                cur = (DomNode*)frag->shadow_host;
                continue;
            }
            return false;
        }
        if (cur == doc_stub) return true;
        if (root && cur == root) return true;
        cur = cur->parent;
    }
    return false;
}

// Backwards-compatible wrapper: when no selection is available at the call
// site, fall back to the thread-local active document.
static bool node_in_active_document(DomNode* n, DomSelection* s) {
    return node_in_selection_doc(n, s);
}

// ============================================================================
// Range methods
// ============================================================================

extern "C" Item js_range_set_start(Item self_v, Item node_arg_v, Item offset_arg_v) {
    DomRange* r = range_from(self_v);
    if (!r) return make_undef();
    DomNode* n = node_arg(node_arg_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_set_start(r, n, (uint32_t)item_to_int(offset_arg_v), &exc)) {
        throw_from_dom_exc(exc, "Range.setStart failed");
        return make_undef();
    }
    range_sync_props(self_v, r);
    return make_undef();
}

extern "C" Item js_range_set_end(Item self_v, Item node_arg_v, Item offset_arg_v) {
    DomRange* r = range_from(self_v);
    if (!r) return make_undef();
    DomNode* n = node_arg(node_arg_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_set_end(r, n, (uint32_t)item_to_int(offset_arg_v), &exc)) {
        throw_from_dom_exc(exc, "Range.setEnd failed");
        return make_undef();
    }
    range_sync_props(self_v, r);
    return make_undef();
}

extern "C" Item js_range_set_start_before(Item self_v, Item node_v) {
    DomRange* r = range_from(self_v); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_set_start_before(r, n, &exc)) {
        throw_from_dom_exc(exc, "Range.setStartBefore failed");
        return make_undef();
    }
    range_sync_props(self_v, r);
    return make_undef();
}

extern "C" Item js_range_set_start_after(Item self_v, Item node_v) {
    DomRange* r = range_from(self_v); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_set_start_after(r, n, &exc)) {
        throw_from_dom_exc(exc, "Range.setStartAfter failed");
        return make_undef();
    }
    range_sync_props(self_v, r);
    return make_undef();
}

extern "C" Item js_range_set_end_before(Item self_v, Item node_v) {
    DomRange* r = range_from(self_v); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_set_end_before(r, n, &exc)) {
        throw_from_dom_exc(exc, "Range.setEndBefore failed");
        return make_undef();
    }
    range_sync_props(self_v, r);
    return make_undef();
}

extern "C" Item js_range_set_end_after(Item self_v, Item node_v) {
    DomRange* r = range_from(self_v); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_set_end_after(r, n, &exc)) {
        throw_from_dom_exc(exc, "Range.setEndAfter failed");
        return make_undef();
    }
    range_sync_props(self_v, r);
    return make_undef();
}

extern "C" Item js_range_collapse(Item self_v, Item to_start_v) {
    DomRange* r = range_from(self_v); if (!r) return make_undef();
    bool to_start = false;
    TypeId t = get_type_id(to_start_v);
    if (t != LMD_TYPE_NULL && t != LMD_TYPE_UNDEFINED) {
        to_start = item_to_bool(to_start_v);
    }
    dom_range_collapse(r, to_start);
    range_sync_props(self_v, r);
    return make_undef();
}

extern "C" Item js_range_select_node(Item self_v, Item node_v) {
    DomRange* r = range_from(self_v); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_select_node(r, n, &exc)) {
        throw_from_dom_exc(exc, "Range.selectNode failed");
        return make_undef();
    }
    range_sync_props(self_v, r);
    return make_undef();
}

extern "C" Item js_range_select_node_contents(Item self_v, Item node_v) {
    DomRange* r = range_from(self_v); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_select_node_contents(r, n, &exc)) {
        throw_from_dom_exc(exc, "Range.selectNodeContents failed");
        return make_undef();
    }
    range_sync_props(self_v, r);
    return make_undef();
}

extern "C" Item js_range_clone_range(Item self_v) {
    DomRange* r = range_from(self_v); if (!r) return ItemNull;
    DomRange* clone = dom_range_clone(r);
    if (!clone) return ItemNull;
    return build_range_object(clone);
}

extern "C" Item js_range_compare_boundary_points(Item self_v, Item how_v, Item other_v) {
    DomRange* r = range_from(self_v); if (!r) return make_int(0);
    int how = item_to_int(how_v);
    DomRange* other = range_from(other_v);
    if (!other) { throw_dom_exception("TypeError", "argument is not a Range"); return make_int(0); }
    const char* exc = nullptr;
    int result = dom_range_compare_boundary_points(
        r, (DomRangeCompareHow)how, other, &exc);
    if (exc) { throw_from_dom_exc(exc, "compareBoundaryPoints failed"); return make_int(0); }
    return make_int(result);
}

extern "C" Item js_range_compare_point(Item self_v, Item node_v, Item offset_v) {
    DomRange* r = range_from(self_v); if (!r) return make_int(0);
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_int(0); }
    const char* exc = nullptr;
    int result = dom_range_compare_point(r, n, (uint32_t)item_to_int(offset_v), &exc);
    if (exc) { throw_from_dom_exc(exc, "comparePoint failed"); return make_int(0); }
    return make_int(result);
}

extern "C" Item js_range_is_point_in_range(Item self_v, Item node_v, Item offset_v) {
    DomRange* r = range_from(self_v); if (!r) return make_bool(false);
    DomNode* n = node_arg(node_v);
    if (!n) return make_bool(false);
    return make_bool(dom_range_is_point_in_range(r, n, (uint32_t)item_to_int(offset_v)));
}

extern "C" Item js_range_intersects_node(Item self_v, Item node_v) {
    DomRange* r = range_from(self_v); if (!r) return make_bool(false);
    DomNode* n = node_arg(node_v);
    if (!n) return make_bool(false);
    return make_bool(dom_range_intersects_node(r, n));
}

extern "C" Item js_range_detach(Item self_v) {
    // per spec, detach() is a no-op (legacy)
    return make_undef();
}

extern "C" Item js_range_to_string(Item self_v) {
    return js_dom_range_to_string_value(self_v);
}

struct RangeClientRectCollector {
    Item array;
    uint32_t count;
    float left;
    float top;
    float right;
    float bottom;
};

static Item js_dom_make_rect(float x, float y, float w, float h) {
    Item rect = js_new_object();
    js_property_set(rect, make_key("x"), make_number_from_float(x));
    js_property_set(rect, make_key("y"), make_number_from_float(y));
    js_property_set(rect, make_key("left"), make_number_from_float(x));
    js_property_set(rect, make_key("top"), make_number_from_float(y));
    js_property_set(rect, make_key("right"), make_number_from_float(x + w));
    js_property_set(rect, make_key("bottom"), make_number_from_float(y + h));
    js_property_set(rect, make_key("width"), make_number_from_float(w));
    js_property_set(rect, make_key("height"), make_number_from_float(h));
    return rect;
}

static void js_range_collect_rect(float x, float y, float w, float h,
                                  void* userdata) {
    RangeClientRectCollector* collector =
        (RangeClientRectCollector*)userdata;
    if (!collector) return;
    js_array_push(collector->array, js_dom_make_rect(x, y, w, h));
    if (collector->count == 0) {
        collector->left = x;
        collector->top = y;
        collector->right = x + w;
        collector->bottom = y + h;
    } else {
        if (x < collector->left) collector->left = x;
        if (y < collector->top) collector->top = y;
        if (x + w > collector->right) collector->right = x + w;
        if (y + h > collector->bottom) collector->bottom = y + h;
    }
    collector->count++;
}

static RangeClientRectCollector js_range_collect_client_rects(DomRange* r) {
    RangeClientRectCollector collector;
    memset(&collector, 0, sizeof(collector));
    collector.array = js_array_new(0);
    if (!r) return collector;
    DomDocument* doc = node_owning_doc(r->start.node);
    if (!doc) doc = node_owning_doc(r->end.node);
    if (doc && !js_dom_force_layout_for_geometry(doc)) return collector;
    if (!dom_range_resolve_layout(r)) return collector;
    dom_range_for_each_rect(r, nullptr, js_range_collect_rect, &collector);
    return collector;
}

extern "C" Item js_range_get_client_rects(Item self_v) {
    RangeClientRectCollector collector =
        js_range_collect_client_rects(range_from(self_v));
    return collector.array;
}

extern "C" Item js_range_get_bounding_client_rect(Item self_v) {
    RangeClientRectCollector collector =
        js_range_collect_client_rects(range_from(self_v));
    if (collector.count == 0) return js_dom_make_rect(0, 0, 0, 0);
    return js_dom_make_rect(collector.left, collector.top,
        collector.right - collector.left, collector.bottom - collector.top);
}

extern "C" Item js_dom_range_to_string_value(Item obj) {
    DomRange* r = range_from(obj);
    if (!r) return make_str("");
    char* s = dom_range_to_string(r);
    if (!s) return make_str("");
    Item it = make_str(s);
    mem_free(s);
    return it;
}

// Phase 4 — mutation methods
extern "C" Item js_range_delete_contents(Item self_v) {
    DomRange* r = range_from(self_v); if (!r) return make_undef();
    const char* exc = nullptr;
    if (!dom_range_delete_contents(r, &exc)) {
        throw_from_dom_exc(exc, "deleteContents failed");
    }
    return make_undef();
}

extern "C" Item js_range_extract_contents(Item self_v) {
    DomRange* r = range_from(self_v); if (!r) return ItemNull;
    const char* exc = nullptr;
    DomElement* frag = dom_range_extract_contents(r, &exc);
    if (exc && !frag) { throw_from_dom_exc(exc, "extractContents failed"); return ItemNull; }
    return frag ? js_dom_wrap_element(frag) : ItemNull;
}

extern "C" Item js_range_clone_contents(Item self_v) {
    DomRange* r = range_from(self_v); if (!r) return ItemNull;
    const char* exc = nullptr;
    DomElement* frag = dom_range_clone_contents(r, &exc);
    if (exc && !frag) { throw_from_dom_exc(exc, "cloneContents failed"); return ItemNull; }
    return frag ? js_dom_wrap_element(frag) : ItemNull;
}

extern "C" Item js_range_insert_node(Item self_v, Item node_v) {
    DomRange* r = range_from(self_v); if (!r) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    const char* exc = nullptr;
    if (!dom_range_insert_node(r, n, &exc)) {
        throw_from_dom_exc(exc, "insertNode failed");
    }
    return make_undef();
}

extern "C" Item js_range_surround_contents(Item self_v, Item node_v) {
    DomRange* r = range_from(self_v); if (!r) return make_undef();
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
    Item obj = vmap_new();
    if (get_type_id(obj) != LMD_TYPE_VMAP || !obj.vmap) return ItemNull;
    // Range wrappers have no Map shell; the host brand is the unwrap invariant.
    obj.vmap->host_type = radiant_dom_range_host_type();
    obj.vmap->host_data = r;
    r->host_wrapper = obj.vmap;
    dom_range_retain(r);  // released in js_dom_selection_reset
    return obj;
}

// Receiver-explicit per-property getters — consumed by the radiant module's
// declared-interface bindings through the Jube host API (DOM3); the strcmp
// dispatch chains they replace are gone.
extern "C" Item js_range_get_start_container(Item self_v) {
    DomRange* r = range_from(self_v);
    return r && r->start.node ? js_dom_wrap_element(r->start.node) : ItemNull;
}

extern "C" Item js_range_get_start_offset(Item self_v) {
    DomRange* r = range_from(self_v);
    return r ? make_int((int64_t)r->start.offset) : ItemNull;
}

extern "C" Item js_range_get_end_container(Item self_v) {
    DomRange* r = range_from(self_v);
    return r && r->end.node ? js_dom_wrap_element(r->end.node) : ItemNull;
}

extern "C" Item js_range_get_end_offset(Item self_v) {
    DomRange* r = range_from(self_v);
    return r ? make_int((int64_t)r->end.offset) : ItemNull;
}

extern "C" Item js_range_get_collapsed(Item self_v) {
    DomRange* r = range_from(self_v);
    return r ? make_bool(dom_range_collapsed(r)) : ItemNull;
}

extern "C" Item js_range_get_common_ancestor(Item self_v) {
    DomRange* r = range_from(self_v);
    if (!r) return ItemNull;
    DomNode* anc = dom_range_common_ancestor(r);
    return anc ? js_dom_wrap_element(anc) : ItemNull;
}

// ============================================================================
// Selection methods
// ============================================================================

extern "C" Item js_selection_get_range_at(Item self_v, Item index_v) {
    DomSelection* s = selection_from(self_v);
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

extern "C" Item js_selection_add_range(Item self_v, Item range_v) {
    DomSelection* s = selection_from(self_v); if (!s) return make_undef();
    DomRange* r = range_from(range_v);
    if (!r) { throw_dom_exception("TypeError", "argument is not a Range"); return make_undef(); }
    // Per spec: if range's root is not the document associated with this,
    // return (do nothing). The WPT tests assert rangeCount is unchanged.
    if (!node_in_active_document(r->start.node, s) ||
        !node_in_active_document(r->end.node, s)) {
        return make_undef();
    }
    const char* exc = nullptr;
    if (!state_store_add_selection_range(s->state, r, &exc)) {
        throw_from_dom_exc(exc, "Selection.addRange failed");
        return make_undef();
    }
    selection_sync_props(self_v, s);
    return make_undef();
}

extern "C" Item js_selection_remove_range(Item self_v, Item range_v) {
    DomSelection* s = selection_from(self_v); if (!s) return make_undef();
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
    const char* exc = nullptr;
    if (!state_store_remove_selection_range(s->state, r, &exc)) {
        throw_from_dom_exc(exc, "Selection.removeRange failed");
        return make_undef();
    }
    selection_sync_props(self_v, s);
    return make_undef();
}

extern "C" Item js_selection_remove_all_ranges(Item self_v) {
    DomSelection* s = selection_from(self_v); if (!s) return make_undef();
    const char* exc = nullptr;
    if (!selection_state_clear(s, &exc)) {
        throw_from_dom_exc(exc, "removeAllRanges failed");
        return make_undef();
    }
    selection_sync_props(self_v, s);
    return make_undef();
}

extern "C" Item js_selection_empty(Item self_v) {
    DomSelection* s = selection_from(self_v); if (!s) return make_undef();
    const char* exc = nullptr;
    if (!selection_state_clear(s, &exc)) {
        throw_from_dom_exc(exc, "empty failed");
        return make_undef();
    }
    selection_sync_props(self_v, s);
    return make_undef();
}

extern "C" Item js_selection_collapse(Item self_v, Item node_v, Item offset_v) {
    DomSelection* s = selection_from(self_v); if (!s) return make_undef();
    DomNode* n = node_arg(node_v);
    // per spec: collapse(null) clears the selection
    if (!n && get_type_id(node_v) == LMD_TYPE_NULL) {
        const char* exc = nullptr;
        if (!selection_state_clear(s, &exc)) {
            throw_from_dom_exc(exc, "Selection.collapse(null) failed");
            return make_undef();
        }
        selection_sync_props(self_v, s);
        return make_undef();
    }
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    // Per spec: throw InvalidNodeTypeError if node is a DocumentType.
    if (n->node_type == DOM_NODE_DOCTYPE) {
        throw_dom_exception("InvalidNodeTypeError",
                            "Selection.collapse: node must not be a DocumentType");
        return make_undef();
    }
    // Per spec: offset bounds check happens BEFORE the document-root check.
    uint32_t off = (uint32_t)item_to_int(offset_v);
    if (off > dom_node_boundary_length(n)) {
        throw_dom_exception("IndexSizeError",
                            "Selection.collapse: offset is out of bounds");
        return make_undef();
    }
    // Per spec: if node's root is not the document associated with this,
    // abort (no-op — the selection is unchanged, no range is added).
    if (!node_in_active_document(n, s)) {
        return make_undef();
    }
    const char* exc = nullptr;
    DomBoundary caret = { n, off };
    if (!selection_state_set(s, &caret, &caret, &exc)) {
        throw_from_dom_exc(exc, "Selection.collapse failed");
        return make_undef();
    }
    extern void js_dom_focus_if_editing_host_for_selection(void* dom_node);
    js_dom_focus_if_editing_host_for_selection((void*)n);
    selection_sync_props(self_v, s);
    return make_undef();
}

extern "C" Item js_selection_set_position(Item self_v, Item node_v, Item offset_v) {
    return js_selection_collapse(self_v, node_v, offset_v);
}

extern "C" Item js_selection_collapse_to_start(Item self_v) {
    DomSelection* s = selection_from(self_v); if (!s) return make_undef();
    DomElement* control = active_text_control_for_selection(s);
    if (control) {
        tc_ensure_init(control);
        FormControlProp* form = control->form;
        uint32_t start = form ? form->selection_start : 0;
        DocState* state = state_for_text_control_selection(s, control);
        state_store_set_text_control_selection(state, control,
            start, start, 0);
        selection_sync_props(self_v, s);
        return make_undef();
    }
    if (s->range_count == 0 || !s->ranges[0]) {
        throw_from_dom_exc("InvalidStateError", "collapseToStart failed");
        return make_undef();
    }
    DomBoundary start = s->ranges[0]->start;
    const char* exc = nullptr;
    if (!selection_state_set(s, &start, &start, &exc)) {
        throw_from_dom_exc(exc, "collapseToStart failed");
        return make_undef();
    }
    selection_sync_props(self_v, s);
    return make_undef();
}

extern "C" Item js_selection_collapse_to_end(Item self_v) {
    DomSelection* s = selection_from(self_v); if (!s) return make_undef();
    DomElement* control = active_text_control_for_selection(s);
    if (control) {
        tc_ensure_init(control);
        FormControlProp* form = control->form;
        uint32_t end = form ? form->selection_end : 0;
        DocState* state = state_for_text_control_selection(s, control);
        state_store_set_text_control_selection(state, control,
            end, end, 0);
        selection_sync_props(self_v, s);
        return make_undef();
    }
    if (s->range_count == 0 || !s->ranges[0]) {
        throw_from_dom_exc("InvalidStateError", "collapseToEnd failed");
        return make_undef();
    }
    DomBoundary end = s->ranges[0]->end;
    const char* exc = nullptr;
    if (!selection_state_set(s, &end, &end, &exc)) {
        throw_from_dom_exc(exc, "collapseToEnd failed");
        return make_undef();
    }
    selection_sync_props(self_v, s);
    return make_undef();
}

extern "C" Item js_selection_extend(Item self_v, Item node_v, Item offset_v) {
    DomSelection* s = selection_from(self_v); if (!s) return make_undef();
    DomNode* n = node_arg(node_v);
    TypeId node_type = get_type_id(node_v);
    if (!n) {
        if (node_type == LMD_TYPE_UNDEFINED) {
            throw_dom_exception("TypeError",
                                "Failed to execute 'extend' on 'Selection': 1 argument required, but only 0 present.");
        } else {
            throw_dom_exception("TypeError",
                                "Failed to execute 'extend' on 'Selection': parameter 1 is not of type 'Node'.");
        }
        return make_undef();
    }
    // Per spec: if node's root is not the document, abort (no-op).
    if (!node_in_active_document(n, s)) {
        return make_undef();
    }
    if (s->range_count == 0 || !s->ranges[0]) {
        throw_from_dom_exc("InvalidStateError", "Selection.extend failed");
        return make_undef();
    }
    if (n->node_type == DOM_NODE_DOCTYPE) {
        throw_dom_exception("InvalidNodeTypeError",
                            "Selection.extend: node must not be a DocumentType");
        return make_undef();
    }
    uint32_t off = (uint32_t)item_to_int(offset_v);
    uint32_t node_len = dom_node_boundary_length(n);
    if (off > node_len) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "Failed to execute 'extend' on 'Selection': The offset %u is larger than the node's length (%u).",
                 off, node_len);
        throw_dom_exception("IndexSizeError", msg);
        return make_undef();
    }
    const char* exc = nullptr;
    DomBoundary anchor = dom_selection_anchor_boundary(s);
    DomBoundary focus = { n, off };
    DomBoundaryOrder order = dom_boundary_compare(&anchor, &focus);
    bool ok = false;
    if (order == DOM_BOUNDARY_DISJOINT) {
        ok = selection_state_set(s, &focus, &focus, &exc);
    } else {
        ok = selection_state_set(s, &anchor, &focus, &exc);
    }
    if (!ok) {
        throw_from_dom_exc(exc, "Selection.extend failed");
        return make_undef();
    }
    selection_sync_props(self_v, s);
    return make_undef();
}

extern "C" Item js_selection_set_base_and_extent(Item self_v, Item anchor_node_v, Item anchor_off_v,
                                                  Item focus_node_v,  Item focus_off_v) {
    DomSelection* s = selection_from(self_v); if (!s) return make_undef();
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
    if (!an && get_type_id(anchor_node_v) == LMD_TYPE_NULL) {
        const char* exc = nullptr;
        if (!selection_state_clear(s, &exc)) {
            throw_from_dom_exc(exc, "setBaseAndExtent clear failed");
            return make_undef();
        }
        selection_sync_props(self_v, s);
        return make_undef();
    }
    if ((an && an->node_type == DOM_NODE_DOCTYPE) ||
        (fn && fn->node_type == DOM_NODE_DOCTYPE)) {
        throw_dom_exception("InvalidNodeTypeError",
                            "setBaseAndExtent: node must not be a DocumentType");
        return make_undef();
    }
    if (an && !fn && get_type_id(focus_node_v) == LMD_TYPE_NULL) {
        uint32_t off = selection_text_offset_from_item(an, anchor_off_v);
        const char* exc = nullptr;
        DomBoundary caret = { an, off };
        if (!selection_state_set(s, &caret, &caret, &exc)) {
            throw_from_dom_exc(exc, "setBaseAndExtent collapse failed");
            return make_undef();
        }
        selection_sync_props(self_v, s);
        return make_undef();
    }
    if (!an || !fn) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    // Per spec: if either node isn't a descendant of the document, abort
    // (selection is left empty — do not add a range).
    if (!node_in_active_document(an, s) || !node_in_active_document(fn, s)) {
        const char* exc = nullptr;
        if (!selection_state_clear(s, &exc)) {
            throw_from_dom_exc(exc, "setBaseAndExtent clear failed");
            return make_undef();
        }
        selection_sync_props(self_v, s);
        return make_undef();
    }
    const char* exc = nullptr;
    DomBoundary anchor = {
        an,
        selection_text_offset_from_item(an, anchor_off_v)
    };
    DomBoundary focus = {
        fn,
        selection_text_offset_from_item(fn, focus_off_v)
    };
    if (!selection_state_set(s, &anchor, &focus, &exc)) {
        throw_from_dom_exc(exc, "setBaseAndExtent failed");
        return make_undef();
    }
    extern void js_dom_focus_if_editing_host_for_selection(void* dom_node);
    js_dom_focus_if_editing_host_for_selection((void*)fn);
    selection_sync_props(self_v, s);
    return make_undef();
}

extern "C" Item js_selection_select_all_children(Item self_v, Item node_v) {
    DomSelection* s = selection_from(self_v); if (!s) return make_undef();
    DomNode* n = node_arg(node_v);
    if (!n) { throw_dom_exception("TypeError", "node is not a Node"); return make_undef(); }
    // Per spec: throw InvalidNodeTypeError if node is a DocumentType.
    if (n->node_type == DOM_NODE_DOCTYPE) {
        throw_dom_exception("InvalidNodeTypeError",
                            "Selection.selectAllChildren: node must not be a DocumentType");
        return make_undef();
    }
    // Per spec: if node's root is not the document associated with this,
    // abort (no-op).
    if (!node_in_active_document(n, s)) {
        return make_undef();
    }
    uint32_t child_count = 0;
    if (n->is_element()) {
        for (DomNode* c = n->as_element()->first_child; c; c = c->next_sibling) {
            child_count++;
        }
    }
    const char* exc = nullptr;
    DomBoundary start = { n, 0 };
    DomBoundary end = { n, child_count };
    if (!selection_state_set(s, &start, &end, &exc)) {
        throw_from_dom_exc(exc, "selectAllChildren failed");
        return make_undef();
    }
    selection_sync_props(self_v, s);
    return make_undef();
}

extern "C" Item js_selection_contains_node(Item self_v, Item node_v, Item allow_partial_v) {
    DomSelection* s = selection_from(self_v); if (!s) return make_bool(false);
    DomNode* n = node_arg(node_v);
    if (!n) {
        throw_dom_exception("TypeError",
                            "Failed to execute 'containsNode' on 'Selection': parameter 1 is not of type 'Node'.");
        return make_undef();
    }
    bool partial = false;
    TypeId t = get_type_id(allow_partial_v);
    if (t != LMD_TYPE_NULL && t != LMD_TYPE_UNDEFINED) partial = item_to_bool(allow_partial_v);
    return make_bool(dom_selection_contains_node(s, n, partial));
}

extern "C" Item js_selection_delete_from_document(Item self_v) {
    DomSelection* s = selection_from(self_v); if (!s) return make_undef();
    const char* exc = nullptr;
    if (!state_store_delete_selection_from_document(s->state, &exc)) {
        throw_from_dom_exc(exc, "deleteFromDocument failed");
        return make_undef();
    }
    selection_sync_props(self_v, s);
    return make_undef();
}

extern "C" Item js_selection_to_string(Item self_v) {
    return js_dom_selection_to_string_value(self_v);
}

extern "C" Item js_dom_selection_to_string_value(Item obj) {
    DomSelection* s = selection_from(obj);
    // Per WPT stringifier_editable_element.tentative.html: when a focused
    // text control has a non-empty selection, getSelection().toString()
    // returns the visible selected substring of the control — even when
    // the document selection itself is empty.
    extern String* js_dom_active_text_control_selected_text(void);
    String* tc_str = js_dom_active_text_control_selected_text();
    if (tc_str) {
        return (Item){.item = s2it(tc_str)};
    }
    if (!s) return make_str("");
    if (dom_selection_range_count(s) == 0) return make_str("");
    DomRange* r = dom_selection_get_range_at(s, 0, nullptr);
    if (!r) return make_str("");
    char* out = dom_range_to_string_ex(r, DOM_STRINGIFY_RENDERED);
    if (!out) return make_str("");
    Item it = make_str(out);
    mem_free(out);
    return it;
}

extern "C" Item js_selection_modify(Item self_v, Item alter_v, Item dir_v, Item gran_v) {
    DomSelection* s = selection_from(self_v); if (!s) return make_undef();
    const char* alter = fn_to_cstr(alter_v);
    const char* dir   = fn_to_cstr(dir_v);
    const char* gran  = fn_to_cstr(gran_v);
    const char* exc = nullptr;
    if (!state_store_modify_selection(s->state, alter, dir, gran, &exc)) {
        throw_from_dom_exc(exc, "Selection.modify failed");
        return make_undef();
    }
    selection_sync_props(self_v, s);
    return make_undef();
}

// Internal-only: force the selection's direction field to a specific value.
// Used by the WPT testdriver Actions shim to model click-induced selections
// which (per Selection API issue 177) are 'none' even when non-collapsed.
// Not exposed to spec'd selection.* surface.
extern "C" Item js_selection_force_direction(Item self_v, Item dir_v) {
    DomSelection* s = selection_from(self_v); if (!s) return make_undef();
    const char* d = fn_to_cstr(dir_v);
    if (!d) return make_undef();
    if (strcmp(d, "none") == 0)          s->direction = DOM_SEL_DIR_NONE;
    else if (strcmp(d, "forward") == 0)  s->direction = DOM_SEL_DIR_FORWARD;
    else if (strcmp(d, "backward") == 0) s->direction = DOM_SEL_DIR_BACKWARD;
    return make_undef();
}

// ============================================================================
// Selection construction / property sync
// ============================================================================

static inline void selection_sync_props(Item /*obj*/, DomSelection* /*s*/) {}

static Item build_selection_object(DomSelection* s) {
    if (!s) return ItemNull;
    Item obj = vmap_new();
    if (get_type_id(obj) != LMD_TYPE_VMAP || !obj.vmap) return ItemNull;
    // Selection wrappers have no Map shell; the host brand is the unwrap invariant.
    obj.vmap->host_type = radiant_dom_selection_host_type();
    obj.vmap->host_data = s;
    s->host_wrapper = obj.vmap;
    return obj;
}

// Receiver-explicit per-property getters (DOM3 declared-interface bindings).
extern "C" Item js_selection_get_anchor_node(Item self_v) {
    DomSelection* s = selection_from(self_v);
    DomNode* n = s ? dom_selection_anchor_node(s) : nullptr;
    return n ? js_dom_wrap_element(n) : ItemNull;
}

extern "C" Item js_selection_get_anchor_offset(Item self_v) {
    DomSelection* s = selection_from(self_v);
    return s ? make_int((int64_t)dom_selection_anchor_offset(s)) : ItemNull;
}

extern "C" Item js_selection_get_focus_node(Item self_v) {
    DomSelection* s = selection_from(self_v);
    DomNode* n = s ? dom_selection_focus_node(s) : nullptr;
    return n ? js_dom_wrap_element(n) : ItemNull;
}

extern "C" Item js_selection_get_focus_offset(Item self_v) {
    DomSelection* s = selection_from(self_v);
    return s ? make_int((int64_t)dom_selection_focus_offset(s)) : ItemNull;
}

extern "C" Item js_selection_get_is_collapsed(Item self_v) {
    DomSelection* s = selection_from(self_v);
    return s ? make_bool(dom_selection_is_collapsed(s)) : ItemNull;
}

extern "C" Item js_selection_get_range_count(Item self_v) {
    DomSelection* s = selection_from(self_v);
    return s ? make_int((int64_t)dom_selection_range_count(s)) : ItemNull;
}

extern "C" Item js_selection_get_type(Item self_v) {
    DomSelection* s = selection_from(self_v);
    return s ? make_str(dom_selection_type(s)) : ItemNull;
}

extern "C" Item js_selection_get_direction(Item self_v) {
    DomSelection* s = selection_from(self_v);
    if (!s) return ItemNull;
    // Selection.direction returns 'none' | 'forward' | 'backward' per the
    // Selection API spec (proposed). 'none' for collapsed or empty selections.
    if (s->range_count == 0 || dom_selection_is_collapsed(s)) return make_str("none");
    switch (s->direction) {
        case DOM_SEL_DIR_FORWARD:  return make_str("forward");
        case DOM_SEL_DIR_BACKWARD: return make_str("backward");
        default:                   return make_str("none");
    }
}

// ============================================================================
// Public entry points
// ============================================================================

extern "C" Item js_dom_create_range(void) {
    DocState* state = get_or_create_state();
    if (!state) {
        log_error("createRange: no document state");
        return ItemNull;
    }
    DomRange* r = dom_range_create(state);
    if (!r) return ItemNull;
    dom_range_link_into_state(state, r);
    return build_range_object(r);
}

extern "C" Item js_dom_create_live_range_from_boundaries(Item start_container,
                                                         int64_t start_offset,
                                                         Item end_container,
                                                         int64_t end_offset,
                                                         const char** out_exception) {
    if (out_exception) *out_exception = nullptr;
    if (start_offset < 0 || end_offset < 0) {
        if (out_exception) *out_exception = "IndexSizeError";
        return ItemNull;
    }

    DomNode* start_node = (DomNode*)js_dom_unwrap_element(start_container);
    DomNode* end_node = (DomNode*)js_dom_unwrap_element(end_container);
    if (!start_node || !end_node) {
        if (out_exception) *out_exception = "InvalidNodeTypeError";
        return ItemNull;
    }

    DocState* state = get_or_create_state();
    if (!state) {
        log_error("create_live_range_from_boundaries: no document state");
        if (out_exception) *out_exception = "InvalidStateError";
        return ItemNull;
    }

    DomRange* r = dom_range_create(state);
    if (!r) {
        if (out_exception) *out_exception = "InvalidStateError";
        return ItemNull;
    }

    const char* exc = nullptr;
    if (!dom_range_set_start(r, start_node, (uint32_t)start_offset, &exc) ||
        !dom_range_set_end(r, end_node, (uint32_t)end_offset, &exc)) {
        dom_range_release(r);
        if (out_exception) *out_exception = exc ? exc : "InvalidStateError";
        return ItemNull;
    }

    dom_range_link_into_state(state, r);
    return build_range_object(r);
}

extern "C" Item js_dom_get_selection(void) {
    DocState* state = get_or_create_state();
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
    Item self = js_get_this();
    void* foreign = js_get_foreign_doc(self);
    if (foreign && js_doc_has_browsing_context(foreign)) {
        void* prev = js_dom_swap_active_document(foreign);
        Item selection = js_dom_get_selection();
        js_dom_restore_active_document(prev);
        return selection;
    }
    return js_dom_get_selection();
}

static Item js_bound_document_get_selection(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    DomDocument* doc = env ? (DomDocument*)(uintptr_t)env[0].item : nullptr;
    if (!doc || !js_doc_has_browsing_context(doc)) return ItemNull;
    void* prev = js_dom_swap_active_document(doc);
    Item selection = js_dom_get_selection();
    js_dom_restore_active_document(prev);
    return selection;
}

extern "C" Item js_dom_get_selection_function_for_document(void* doc) {
    Item* env = js_alloc_env(1);
    if (!env) return js_new_function((void*)js_global_get_selection, 0);
    env[0] = (Item){.item = (uint64_t)(uintptr_t)doc};
    return js_new_closure((void*)js_bound_document_get_selection, 0, env, 1);
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
    // Stage 4C: `window.document` must resolve to the document proxy. Bare
    // `document` is special-cased in the transpiler (js_mir_expression_lowering
    // rewrites the identifier to a direct proxy access), so it never becomes a
    // real property on the window/global object — which left `window.document`
    // (a plain member access) `undefined`, breaking e.g.
    // `window.document.createRange()` in dom-bridge. Install it explicitly. The
    // proxy is a stable wrapper whose methods route to the current main document,
    // and it is the same item bare `document` yields, so `window.document ===
    // document` holds.
    Item doc_proxy = js_get_document_object_value();
    js_property_set(global, make_key("document"), doc_proxy);
    if (get_type_id(existing) == LMD_TYPE_MAP)
        js_property_set(existing, make_key("document"), doc_proxy);

    Item flush_fn = js_new_function((void*)js_dom_flush_selectionchange, 0);
    js_property_set(global, make_key("__lambdaFlushSelectionChange"), flush_fn);
    if (get_type_id(existing) == LMD_TYPE_MAP)
        js_property_set(existing, make_key("__lambdaFlushSelectionChange"), flush_fn);

    // Install placeholder Selection / Range constructors so `instanceof Selection`
    // and feature-detection (`window.Selection`) succeed. The constructors are
    // never actually invoked by typical WPT code (which uses document.createRange
    // / getSelection); identity comes from their function names plus DOM host
    // fast paths in js_instanceof_classname.
    Item sel_ctor   = js_new_function((void*)js_global_get_selection, 0);
    Item range_ctor = js_new_function((void*)js_dom_create_range, 0);
    js_set_function_name(sel_ctor, make_key("Selection"));
    js_set_function_name(range_ctor, make_key("Range"));
    js_property_set(global, make_key("Selection"), sel_ctor);
    js_property_set(global, make_key("Range"),     range_ctor);

    // Install Selection.prototype and Range.prototype with method stubs so
    // WPT idl checks like `Selection.prototype.deleteFromDocument.length`
    // succeed. The methods themselves are never invoked through the
    // prototype path (instances are DOM resources and dispatch through their
    // own get_property hooks); these are pure idl shape.
    Item sel_proto = js_property_get(sel_ctor, make_key("prototype"));
    if (get_type_id(sel_proto) != LMD_TYPE_MAP) {
        extern Item js_new_object();
        sel_proto = js_new_object();
        js_property_set(sel_ctor, make_key("prototype"), sel_proto);
    }
    Item range_proto = js_property_get(range_ctor, make_key("prototype"));
    if (get_type_id(range_proto) != LMD_TYPE_MAP) {
        extern Item js_new_object();
        range_proto = js_new_object();
        js_property_set(range_ctor, make_key("prototype"), range_proto);
    }
    // DOM3: force the jube type prototypes now that the ctors' .prototype
    // objects exist — this publishes the declared method function objects onto
    // Range.prototype / Selection.prototype (IDL shape, .length probes) before
    // any script can read them.
    jube_type_prototype((const JubeTypeDef*)radiant_dom_range_host_type());
    jube_type_prototype((const JubeTypeDef*)radiant_dom_selection_host_type());

    // Set document.defaultView = window so DOM tests' sanity checks pass.
    Item doc = js_get_document_object_value();
    js_document_proxy_set_property(make_key("defaultView"), global);
    (void)doc;

    log_debug("js_dom_selection: installed global getSelection / Selection / Range");
}

// ----------------------------------------------------------------------------
// Phase 8D: selectionchange event bridge
// ----------------------------------------------------------------------------
// Called from radiant/dom_range.cpp's notify_selection_changed() (a weak
// symbol) after every spec mutation funneled through StateStore/DOM selection
// facade writers.
// Per the WHATWG HTML "selectionchange" task: coalesce multiple synchronous
// mutations into a single async dispatch, fire on the document.
static Item _wpt_selectionchange_fire(Item this_val, Item* args, int argc) {
    (void)this_val; (void)args; (void)argc;
    DomDocument* doc = (DomDocument*)js_dom_get_document();
    if (!doc || !doc->state) return ItemNull;
    DocState* state = doc->state;
    if (!state->selectionchange_pending) return ItemNull;
    state->selectionchange_pending = false;
    state->selection_event_seq = state->selection_mutation_seq;
    Item ev = js_create_event("selectionchange", /*bubbles=*/false,
                              /*cancelable=*/false);
    Item doc_item = js_get_document_object_value();
    js_dom_dispatch_event(doc_item, ev);
    return ItemNull;
}

static Item js_dom_flush_selectionchange(Item this_val, Item* args, int argc) {
    return _wpt_selectionchange_fire(this_val, args, argc);
}

extern "C" void js_dom_queue_selectionchange(DomSelection* sel) {
    if (!sel || !sel->state) return;
    if (!js_input || !js_input->pool) return;
    DocState* state = sel->state;
    // Coalesce before touching the document runtime. In tight DOM-only loops
    // (e.g. WPT collapse), repeated js_dom_set_document() dominates runtime.
    if (state->selectionchange_pending) return;

    DomBoundary anchor = dom_selection_anchor_boundary(sel);
    DomBoundary focus = dom_selection_focus_boundary(sel);
    DomDocument* doc = nullptr;
    if (anchor.node) doc = node_owning_doc(anchor.node);
    if (!doc && focus.node) doc = node_owning_doc(focus.node);
    if (!doc) doc = (DomDocument*)js_dom_get_document();
    JsDocRuntimeScope scope;
    if (!js_doc_runtime_enter_if_needed(doc, &scope)) return;
    state->selectionchange_pending = true;
    Item cb = js_new_function((void*)_wpt_selectionchange_fire, 0);
    js_setTimeout(cb, (Item){.item = i2it(0)});
    js_doc_runtime_exit(&scope);
}

// ----------------------------------------------------------------------------
// Phase 8E: per-text-control selectionchange dispatch
// ----------------------------------------------------------------------------
// Called from radiant/text_control.cpp after every programmatic selection
// mutation on an <input>/<textarea> (e.g. setSelectionRange, value setter,
// editing). Coalesces per-element via FormControlProp::tc_sc_pending and
// drains the whole pending list in a single setTimeout(0) callback.
static Item _tc_selectionchange_drain(Item this_val, Item* args, int argc) {
    (void)this_val; (void)args; (void)argc;
    DomDocument* doc = (DomDocument*)js_dom_get_document();
    JsDocRuntimeScope scope;
    if (!js_doc_runtime_enter_if_needed(doc, &scope)) return ItemNull;
    DocState* state = get_or_create_state();
    if (!state) {
        js_doc_runtime_exit(&scope);
        return ItemNull;
    }
    DomElement* head = state->tc_selectionchange_head;
    state->tc_selectionchange_head = nullptr;
    state->tc_selectionchange_drain_scheduled = false;
    while (head) {
        DomElement* next = nullptr;
        FormControlProp* f = head->form;
        if (f) {
            next = f->tc_sc_next_pending;
            f->tc_sc_next_pending = nullptr;
            f->tc_sc_pending = 0;
        }
        // Per HTML, selectionchange on text controls fires on the element
        // AND is observable at document. Spec says bubbles=false but two
        // separate dispatches; we use bubbles=true to deliver both with a
        // single event whose .target remains the element — observably
        // equivalent for all WPT tests we exercise.
        Item ev = js_create_event("selectionchange", /*bubbles=*/true,
                                  /*cancelable=*/false);
        js_dom_dispatch_event(js_dom_wrap_element(head), ev);
        head = next;
    }
    js_doc_runtime_exit(&scope);
    return ItemNull;
}

extern "C" void js_dom_queue_textcontrol_selectionchange(DomElement* elem) {
    if (!elem) return;
    FormControlProp* f = elem->form;
    if (!f) return;
    if (!js_input || !js_input->pool) return;
    DomDocument* doc = elem->doc;
    JsDocRuntimeScope scope;
    if (!js_doc_runtime_enter_if_needed(doc, &scope)) return;
    DocState* state = get_or_create_state();
    if (!state) {
        js_doc_runtime_exit(&scope);
        return;
    }
    if (f->tc_sc_pending) {
        js_doc_runtime_exit(&scope);
        return;
    }
    f->tc_sc_pending = 1;
    f->tc_sc_next_pending = state->tc_selectionchange_head;
    state->tc_selectionchange_head = elem;
    if (state->tc_selectionchange_drain_scheduled) {
        js_doc_runtime_exit(&scope);
        return;
    }
    state->tc_selectionchange_drain_scheduled = true;
    Item cb = js_new_function((void*)_tc_selectionchange_drain, 0);
    js_setTimeout(cb, (Item){.item = i2it(0)});
    js_doc_runtime_exit(&scope);
}

extern "C" void js_dom_dispatch_textcontrol_select(DomElement* elem) {
    if (!elem) return;
    if (!js_input || !js_input->pool) return;
    DomDocument* doc = elem->doc ? elem->doc : (DomDocument*)js_dom_get_document();
    JsDocRuntimeScope scope;
    if (!js_doc_runtime_enter_if_needed(doc, &scope)) return;
    Item ev = js_create_event("select", /*bubbles=*/true, /*cancelable=*/false);
    js_dom_dispatch_event(js_dom_wrap_element(elem), ev);
    js_doc_runtime_exit(&scope);
}

extern "C" void js_dom_selection_reset(void) {
    // DOM3: cached jube prototypes/method Items seed off this runtime's global
    // constructors; drop them so the next runtime re-seeds against fresh globals.
    jube_interface_runtime_reset();

    // Native lifetime is owned by the per-document DocState. Walk the
    // current document's live ranges, drop our JS-side retain, and clear
    // host_wrapper back-pointers so a fresh document starts clean.
    DomDocument* doc = (DomDocument*)js_dom_get_document();
    if (!doc || !doc->state) return;
    DocState* state = doc->state;
    DomRange* r = state->live_ranges;
    while (r) {
        DomRange* next = r->next;
        r->host_wrapper = nullptr;
        dom_range_release(r);  // pairs with retain in build_range_object
        r = next;
    }
    if (state->dom_selection) state->dom_selection->host_wrapper = nullptr;
}
