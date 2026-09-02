/**
 * dom_core.cpp — uniform-signature bodies for the DOM operation catalog.
 *
 * ES39/ES40 (vibe/Lambda_Design_DOM_Host_API.md). Each `dom_core_*` function
 * is a core operation: either it *is* the mechanism or it is a one-call
 * delegation to the ordinal executor / property protocol in dom_ops.h (the
 * realm-shared entries, D7.4.4). Each `dom_fp_*` function is the native fast
 * path of a DERIVED row and must equal that row's derivation string (ES43):
 * the oracle tests in test/lambda/dom_derive_*.ls hold it to that.
 *
 * These bodies were lifted from dom_module.cpp (F26) rather than copied: the
 * module now registers the catalog and owns no bodies of its own.
 *
 * `dom_core_parse_fragment` lives in dom.cpp beside the innerHTML setter
 * whose parse loop it shares; it is declared in dom_core.h like the rest.
 */

#include "../lambda-data.hpp"
#include "dom.h"
#include "dom_ops.h"
#include "dom_core.h"
#include "dom_events.h"
#include "../input/css/dom_element.hpp"
#include "../input/css/dom_node.hpp"
#include "../io/mark_builder.hpp"
#include "../js/js_runtime.h"
#include <climits>
#include <cstring>

// ---------------------------------------------------------------------------
// Delegation helpers
// ---------------------------------------------------------------------------

static Item dom_op0(Item node, JubeDomElementOperation op) {
    return dom_element_operation_impl(node, op, nullptr, 0);
}

static Item dom_op1(Item node, JubeDomElementOperation op, Item a) {
    Item args[1] = { a };
    return dom_element_operation_impl(node, op, args, 1);
}

static Item dom_op2(Item node, JubeDomElementOperation op, Item a, Item b) {
    Item args[2] = { a, b };
    return dom_element_operation_impl(node, op, args, 2);
}

static Item dom_op3(Item node, JubeDomElementOperation op, Item a, Item b, Item c) {
    Item args[3] = { a, b, c };
    return dom_element_operation_impl(node, op, args, 3);
}

/**
 * Read a DOM IDL property by its spec (camelCase) name.
 *
 * The property protocol answers JS `undefined` when a property does not exist
 * on this node kind -- Text has no firstElementChild, and CharacterData has no
 * children -- because that protocol is realm-shared (D7.4.4) and undefined is
 * what a JS caller must see. Lambda has no undefined: absence is null. So the
 * catalog's reads normalise at this boundary, and only here; the JS door keeps
 * undefined by calling dom_get_property_impl itself.
 *
 * Without this, `children(text_node)` returned `[undefined]` rather than `[]`
 * (the link walk below stops on null, and undefined is not null) and every
 * element-traversal read on a text node answered undefined instead of null --
 * both found by the ES43 traversal oracle, not by inspection.
 */
static Item dom_prop_get(Item node, const char* name) {
    Item v = dom_get_property_impl(node, js_name_item(name));
    return get_type_id(v) == LMD_TYPE_UNDEFINED ? ItemNull : v;
}

static DomDocument* dom_document_of(Item node_item) {
    DomNode* node = (DomNode*)dom_unwrap_element(node_item);
    if (!node) return nullptr;
    return node->is_element() ? ((DomElement*)node)->doc : nullptr;
}

static const char* dom_cstr_or_null(Item v) {
    return get_type_id(v) == LMD_TYPE_STRING ? fn_to_cstr(v) : nullptr;
}

static double dom_number_of(Item v) {
    TypeId t = get_type_id(v);
    return t == LMD_TYPE_FLOAT ? it2d(v) : t == LMD_TYPE_INT ? (double)it2i(v) : 0.0;
}

// DOM spec node-type codes a script may mint. Fragments are elements tagged
// "#document-fragment" in this DOM, so DOM_NODE_* has no member for them; the
// spec constant is the catalog's contract, not the struct's.
enum { DOM_CORE_KIND_DOCUMENT_FRAGMENT = 11 };

// ===========================================================================
// CORE
// ===========================================================================

// --- node reads: the property protocol is the one reader of node links
extern "C" Item dom_core_node_type(Item n)        { return dom_prop_get(n, "nodeType"); }
extern "C" Item dom_core_node_name(Item n)        { return dom_prop_get(n, "nodeName"); }
extern "C" Item dom_core_node_value(Item n)       { return dom_prop_get(n, "nodeValue"); }
extern "C" Item dom_core_parent_node(Item n)      { return dom_prop_get(n, "parentNode"); }
extern "C" Item dom_core_first_child(Item n)      { return dom_prop_get(n, "firstChild"); }
extern "C" Item dom_core_last_child(Item n)       { return dom_prop_get(n, "lastChild"); }
extern "C" Item dom_core_next_sibling(Item n)     { return dom_prop_get(n, "nextSibling"); }
extern "C" Item dom_core_previous_sibling(Item n) { return dom_prop_get(n, "previousSibling"); }
extern "C" Item dom_core_owner_document(Item n)   { return dom_prop_get(n, "ownerDocument"); }
// identity: `==` cannot express it (S5.1.4 + zero-entry wrappers), so the DOM
// supplies it as an operation, exactly as Node.isSameNode() does.
extern "C" Item dom_core_same_node(Item a, Item b)  { return dom_op1(a, JUBE_DOM_IS_SAME_NODE, b); }

// --- attributes
extern "C" Item dom_core_get_attribute(Item n, Item name) {
    return dom_op1(n, JUBE_DOM_GET_ATTRIBUTE, name);
}
extern "C" Item dom_core_set_attribute(Item n, Item name, Item value) {
    return dom_op2(n, JUBE_DOM_SET_ATTRIBUTE, name, value);
}
extern "C" Item dom_core_remove_attribute(Item n, Item name) {
    return dom_op1(n, JUBE_DOM_REMOVE_ATTRIBUTE, name);
}
extern "C" Item dom_core_attribute_names(Item n) {
    return dom_op0(n, JUBE_DOM_GET_ATTRIBUTE_NAMES);
}

// --- tree mutation
// create_node is the one creator: element (1), text (3), comment (8) and
// fragment (11) are the four node kinds a script can mint; everything else
// (doctype, document) is parser-made.
extern "C" Item dom_core_create_node(Item doc, Item type, Item name, Item data) {
    DomDocument* document = dom_document_of(doc);
    if (!document || !document->input) return ItemNull;
    int64_t kind = get_type_id(type) == LMD_TYPE_INT ? it2i(type) : 0;
    const char* tag = dom_cstr_or_null(name);
    const char* text = dom_cstr_or_null(data);
    MarkBuilder builder(document->input);
    switch (kind) {
    case DOM_NODE_ELEMENT: {
        if (!tag) return ItemNull;
        Item backing = builder.element(tag).final();
        DomElement* elem = dom_element_create(document, tag, backing.element);
        return elem ? dom_wrap_element(elem) : ItemNull;
    }
    case DOM_NODE_TEXT: {
        const char* s = text ? text : "";
        DomText* t = DomText::create_detached_copy(document, s, strlen(s));
        return t ? dom_wrap_element((void*)t) : ItemNull;
    }
    case DOM_NODE_COMMENT: {
        Item backing = builder.element("#comment").text(text ? text : "").final();
        DomNode* c = (DomNode*)dom_comment_create_detached(backing.element, document);
        return c ? dom_wrap_element((void*)c) : ItemNull;
    }
    case DOM_CORE_KIND_DOCUMENT_FRAGMENT: {
        DomElement* f = dom_element_create(document, "#document-fragment", nullptr);
        return f ? dom_wrap_element(f) : ItemNull;
    }
    default:
        return ItemNull;
    }
}
extern "C" Item dom_core_insert_before(Item parent, Item node, Item ref) {
    return dom_op2(parent, JUBE_DOM_INSERT_BEFORE, node, ref);
}
extern "C" Item dom_core_remove_child(Item parent, Item node) {
    return dom_op1(parent, JUBE_DOM_REMOVE_CHILD, node);
}
// replaceData(0, +inf, data): the spec clamps count to the data length, so one
// call rewrites the whole node value with a single mutation record.
extern "C" Item dom_core_set_node_value(Item n, Item data) {
    Item zero = { .item = i2it(0) };
    Item all = { .item = i2it(INT_MAX) };
    return dom_op3(n, JUBE_DOM_REPLACE_DATA, zero, all, data);
}

// --- match / serialize (parse_fragment: dom.cpp)
extern "C" Item dom_core_matches(Item n, Item selector) {
    return dom_op1(n, JUBE_DOM_MATCHES, selector);
}
extern "C" Item dom_core_serialize(Item n, Item outer) {
    bool is_outer = get_type_id(outer) == LMD_TYPE_BOOL && it2b(outer);
    return dom_prop_get(n, is_outer ? "outerHTML" : "innerHTML");
}

// --- geometry
extern "C" Item dom_core_bounding_box(Item n) {
    return dom_op0(n, JUBE_DOM_GET_BOUNDING_CLIENT_RECT);
}
extern "C" Item dom_core_client_rects(Item n) {
    return dom_op0(n, JUBE_DOM_GET_CLIENT_RECTS);
}
extern "C" Item dom_core_scroll_state(Item n) {
    Item out = js_new_object();
    js_set_key_cstr(out, "x", dom_prop_get(n, "scrollLeft"));
    js_set_key_cstr(out, "y", dom_prop_get(n, "scrollTop"));
    return out;
}
extern "C" Item dom_core_set_scroll_state(Item n, Item x, Item y) {
    return dom_op2(n, JUBE_DOM_SCROLL_TO, x, y);
}
extern "C" Item dom_core_element_from_point(Item doc, Item x, Item y) {
    DomDocument* document = dom_document_of(doc);
    if (!document) return ItemNull;
    float fx = (float)dom_number_of(x), fy = (float)dom_number_of(y);
    void* hit = dom_document_element_from_point_native(document, fx, fy);
    return hit ? dom_wrap_element(hit) : ItemNull;
}
extern "C" Item dom_core_scroll_into_view(Item n) {
    return dom_op0(n, JUBE_DOM_SCROLL_INTO_VIEW);
}

// --- range / selection composite reads
extern "C" Item js_range_get_start_container(Item self_v);
extern "C" Item js_range_get_start_offset(Item self_v);
extern "C" Item js_range_get_end_container(Item self_v);
extern "C" Item js_range_get_end_offset(Item self_v);
extern "C" Item js_selection_get_anchor_node(Item self_v);
extern "C" Item js_selection_get_anchor_offset(Item self_v);
extern "C" Item js_selection_get_focus_node(Item self_v);
extern "C" Item js_selection_get_focus_offset(Item self_v);

extern "C" Item dom_core_range_boundaries(Item r) {
    Item out = js_new_object();
    js_set_key_cstr(out, "start_container", js_range_get_start_container(r));
    js_set_key_cstr(out, "start_offset", js_range_get_start_offset(r));
    js_set_key_cstr(out, "end_container", js_range_get_end_container(r));
    js_set_key_cstr(out, "end_offset", js_range_get_end_offset(r));
    return out;
}
extern "C" Item dom_core_selection_boundaries(Item s) {
    Item out = js_new_object();
    js_set_key_cstr(out, "anchor_node", js_selection_get_anchor_node(s));
    js_set_key_cstr(out, "anchor_offset", js_selection_get_anchor_offset(s));
    js_set_key_cstr(out, "focus_node", js_selection_get_focus_node(s));
    js_set_key_cstr(out, "focus_offset", js_selection_get_focus_offset(s));
    return out;
}

// --- style
extern "C" Item dom_get_computed_style(Item elem_item, Item pseudo_item);
extern "C" Item dom_computed_style_get_property(Item style_item, Item prop_name);
extern "C" Item dom_core_computed_style(Item n, Item prop) {
    Item style = dom_get_computed_style(n, ItemNull);
    return get_type_id(style) == LMD_TYPE_NULL ? ItemNull
         : dom_computed_style_get_property(style, prop);
}

// --- listeners: the void-returning core entries under the uniform shape
extern "C" Item dom_add_event_listener_body(Item n, Item type, Item fn, Item opts) {
    dom_add_event_listener(n, type, fn, opts);
    return ItemNull;
}
extern "C" Item dom_remove_event_listener_body(Item n, Item type, Item fn, Item opts) {
    dom_remove_event_listener(n, type, fn, opts);
    return ItemNull;
}

// ===========================================================================
// DERIVED fast paths — each equals its derivation in dom_api.def
// ===========================================================================

extern "C" Item dom_fp_first_element_child(Item n)     { return dom_prop_get(n, "firstElementChild"); }
extern "C" Item dom_fp_last_element_child(Item n)      { return dom_prop_get(n, "lastElementChild"); }
extern "C" Item dom_fp_next_element_sibling(Item n)    { return dom_prop_get(n, "nextElementSibling"); }
extern "C" Item dom_fp_previous_element_sibling(Item n){ return dom_prop_get(n, "previousElementSibling"); }
extern "C" Item dom_fp_parent_element(Item n)          { return dom_prop_get(n, "parentElement"); }
// root_node IS its derivation: JUBE_DOM_GET_ROOT_NODE is answered on the
// module side (radiant_dom_bridge.cpp), never by the core executor, so the
// ordinal is null from a Lambda caller. Walking parent_node is the mechanism.
// Without a document wrapper (ESO93) the walk ends at the document element.
extern "C" Item dom_fp_root_node(Item n) {
    Item cur = n;
    for (Item p = dom_prop_get(cur, "parentNode"); get_type_id(p) != LMD_TYPE_NULL;
         p = dom_prop_get(cur, "parentNode")) {
        cur = p;
    }
    return cur;
}
extern "C" Item dom_fp_contains(Item a, Item b)        { return dom_op1(a, JUBE_DOM_CONTAINS, b); }
extern "C" Item dom_fp_equal_node(Item a, Item b)      { return dom_op1(a, JUBE_DOM_IS_EQUAL_NODE, b); }

// children / child_nodes: snapshot arrays (S9.2.2) built from the core links,
// which is exactly the derivation; the JS live collections are not used.
static Item dom_link_snapshot(Item n, const char* first, const char* next) {
    Item out = js_array_new(0);
    for (Item c = dom_prop_get(n, first); get_type_id(c) != LMD_TYPE_NULL;
         c = dom_prop_get(c, next)) {
        js_array_push(out, c);
    }
    return out;
}
extern "C" Item dom_fp_children(Item n)    { return dom_link_snapshot(n, "firstElementChild", "nextElementSibling"); }
extern "C" Item dom_fp_child_nodes(Item n) { return dom_link_snapshot(n, "firstChild", "nextSibling"); }

extern "C" Item dom_fp_append_child(Item parent, Item child) {
    return dom_op1(parent, JUBE_DOM_APPEND_CHILD, child);
}
extern "C" Item dom_fp_remove(Item n) {
    return dom_op0(n, JUBE_DOM_REMOVE);
}
extern "C" Item dom_fp_replace_child(Item parent, Item new_node, Item old_node) {
    return dom_op2(parent, JUBE_DOM_REPLACE_CHILD, new_node, old_node);
}
extern "C" Item dom_fp_create_element(Item doc, Item tag) {
    Item kind = { .item = i2it(DOM_NODE_ELEMENT) };
    return dom_core_create_node(doc, kind, tag, ItemNull);
}
extern "C" Item dom_fp_create_text_node(Item doc, Item data) {
    Item kind = { .item = i2it(DOM_NODE_TEXT) };
    return dom_core_create_node(doc, kind, ItemNull, data);
}
extern "C" Item dom_fp_clone_node(Item n, Item deep) {
    return dom_op1(n, JUBE_DOM_CLONE_NODE, deep);
}
extern "C" Item dom_fp_text_content(Item n) {
    return dom_prop_get(n, "textContent");
}
extern "C" Item dom_fp_query_selector(Item root, Item selector) {
    return dom_op1(root, JUBE_DOM_QUERY_SELECTOR, selector);
}
extern "C" Item dom_fp_query_selector_all(Item root, Item selector) {
    return dom_op1(root, JUBE_DOM_QUERY_SELECTOR_ALL, selector);
}
extern "C" Item dom_fp_closest(Item n, Item selector) {
    return dom_op1(n, JUBE_DOM_CLOSEST, selector);
}
extern "C" Item dom_fp_get_element_by_id(Item root, Item id) {
    return dom_op1(root, JUBE_DOM_GET_ELEMENT_BY_ID, id);
}
extern "C" Item dom_fp_has_attribute(Item n, Item name) {
    return dom_op1(n, JUBE_DOM_HAS_ATTRIBUTE, name);
}
extern "C" Item dom_fp_inner_html(Item n) { return dom_prop_get(n, "innerHTML"); }
extern "C" Item dom_fp_outer_html(Item n) { return dom_prop_get(n, "outerHTML"); }
