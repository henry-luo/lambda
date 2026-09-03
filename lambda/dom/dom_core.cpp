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
#include "realm/dom_realm.h"
#include "../runtime/lambda-root-frame.hpp"
#include "dom.h"
#include "dom_ops.h"
#include "dom_core.h"
#include "dom_engine.h"
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

extern "C" Item dom_absent_to_null(Item v) {
    return get_type_id(v) == LMD_TYPE_UNDEFINED ? ItemNull : v;
}

static Item dom_op0(Item node, JubeDomElementOperation op) {
    return dom_absent_to_null(dom_element_operation_impl(node, op, nullptr, 0));
}

static Item dom_op1(Item node, JubeDomElementOperation op, Item a) {
    Item args[1] = { a };
    return dom_absent_to_null(dom_element_operation_impl(node, op, args, 1));
}

static Item dom_op2(Item node, JubeDomElementOperation op, Item a, Item b) {
    Item args[2] = { a, b };
    return dom_absent_to_null(dom_element_operation_impl(node, op, args, 2));
}

static Item dom_op3(Item node, JubeDomElementOperation op, Item a, Item b, Item c) {
    Item args[3] = { a, b, c };
    return dom_absent_to_null(dom_element_operation_impl(node, op, args, 3));
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
    // Accepts a node or the document proxy: `create_node(owner_document(n), ...)`
    // must work, and owner_document answers the proxy (ESO93).
    return (DomDocument*)dom_document_from_item(node_item);
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

// The weak half of the document-loading seam declared in dom.h: a runtime with
// no engine linked has nowhere to load a document from, and says so.
extern "C" __attribute__((weak)) void* dom_engine_load_document_native(const char* path) {
    (void)path;
    return nullptr;
}

extern "C" __attribute__((weak)) void dom_engine_bind_host(const void* host_api) {
    (void)host_api;
}

// ---------------------------------------------------------------------------
// Engine-provided catalog rows (F32).
//
// A row flagged DOM_F_ENGINE is one the *host* must implement: state, event
// dispatch, focus, text controls, editing. Those bodies live in the engine
// above this link target, so each is reached through a weak provider seam --
// the same shape as dom.load. A runtime linked without an engine gets the
// default here and answers absence instead of failing to link.
//
// These are what let `dom.*` cover what `radiant.*` covers, which is the
// prerequisite for migrating the behaviour package off `radiant.*` (ES44).
// ---------------------------------------------------------------------------
#define DOM_ENGINE_SEAM_0(name) \
    extern "C" __attribute__((weak)) Item dom_engine_##name(void) { return ItemNull; }
#define DOM_ENGINE_SEAM_4(name) \
    extern "C" __attribute__((weak)) Item dom_engine_##name(Item a, Item b, Item c, Item d) { \
        (void)a; (void)b; (void)c; (void)d; return ItemNull; }
#define DOM_ENGINE_SEAM_1(name) \
    extern "C" __attribute__((weak)) Item dom_engine_##name(Item a) { \
        (void)a; return ItemNull; }
#define DOM_ENGINE_SEAM_2(name) \
    extern "C" __attribute__((weak)) Item dom_engine_##name(Item a, Item b) { \
        (void)a; (void)b; return ItemNull; }
#define DOM_ENGINE_SEAM_3(name) \
    extern "C" __attribute__((weak)) Item dom_engine_##name(Item a, Item b, Item c) { \
        (void)a; (void)b; (void)c; return ItemNull; }

DOM_ENGINE_SEAM_2(get_state)
DOM_ENGINE_SEAM_3(set_state)
DOM_ENGINE_SEAM_1(request_change)
DOM_ENGINE_SEAM_2(dispatch)
DOM_ENGINE_SEAM_1(focused)
DOM_ENGINE_SEAM_2(focus_set)
DOM_ENGINE_SEAM_1(activate_popover)
DOM_ENGINE_SEAM_3(caret_operation)
DOM_ENGINE_SEAM_1(clear_ime_preedit)
DOM_ENGINE_SEAM_0(clipboard_text)
DOM_ENGINE_SEAM_1(context_menu_target)
DOM_ENGINE_SEAM_2(edit_insert_at_boundary)
DOM_ENGINE_SEAM_1(edit_insert_break)
DOM_ENGINE_SEAM_4(edit_replace_range)
DOM_ENGINE_SEAM_1(edit_split_block)
DOM_ENGINE_SEAM_2(key_intent)
DOM_ENGINE_SEAM_3(navigation_destination)
DOM_ENGINE_SEAM_2(open_context_menu)
DOM_ENGINE_SEAM_1(request_navigation)
DOM_ENGINE_SEAM_2(set_caret)
DOM_ENGINE_SEAM_3(set_ime_preedit)
DOM_ENGINE_SEAM_3(set_password_reveal)
DOM_ENGINE_SEAM_1(tc_value)
DOM_ENGINE_SEAM_1(ime_preedit)
DOM_ENGINE_SEAM_1(tc_selection_start)
DOM_ENGINE_SEAM_1(tc_selection_end)
DOM_ENGINE_SEAM_1(edit_node)
DOM_ENGINE_SEAM_1(edit_start)
DOM_ENGINE_SEAM_1(edit_end)
DOM_ENGINE_SEAM_1(is_focusable)
DOM_ENGINE_SEAM_4(dispatch_event)
DOM_ENGINE_SEAM_1(caret_surface)
DOM_ENGINE_SEAM_1(focus_candidates)
DOM_ENGINE_SEAM_1(check_validity)
DOM_ENGINE_SEAM_1(close_context_menu)
DOM_ENGINE_SEAM_1(custom_validity)
DOM_ENGINE_SEAM_1(dom_delete_dom_range)
DOM_ENGINE_SEAM_1(dom_edit_text)
DOM_ENGINE_SEAM_2(dom_insert_html)
DOM_ENGINE_SEAM_2(dom_range_format)
DOM_ENGINE_SEAM_2(dom_replace_dom_range)
DOM_ENGINE_SEAM_4(dom_replace_range)
DOM_ENGINE_SEAM_4(dom_unwrap_range)
DOM_ENGINE_SEAM_4(dom_wrap_range)
DOM_ENGINE_SEAM_1(dropdown_open)
DOM_ENGINE_SEAM_1(embedded_document_root)
DOM_ENGINE_SEAM_1(embedding_element)
DOM_ENGINE_SEAM_0(form_boundary)
DOM_ENGINE_SEAM_2(form_entries)
DOM_ENGINE_SEAM_1(form_url)
DOM_ENGINE_SEAM_1(hover_index)
DOM_ENGINE_SEAM_1(option_count)
DOM_ENGINE_SEAM_1(range_max)
DOM_ENGINE_SEAM_1(range_min)
DOM_ENGINE_SEAM_1(range_value)
DOM_ENGINE_SEAM_1(reset_form)
DOM_ENGINE_SEAM_2(scroll_operation)
DOM_ENGINE_SEAM_1(selected_index)
DOM_ENGINE_SEAM_2(set_dropdown_open)
DOM_ENGINE_SEAM_2(set_hover_index)
DOM_ENGINE_SEAM_2(set_selected_index)
DOM_ENGINE_SEAM_2(submit_event)
DOM_ENGINE_SEAM_1(value_at_focus)

// `dom.load`: the engine parses, the core wraps. The result is the document
// node, which since ESO101 answers both the Document's properties and the
// Node's -- so a script can query straight off what load returns.
extern "C" Item dom_engine_load_document(Item path) {
    const char* p = dom_cstr_or_null(path);
    if (!p) return ItemNull;
    void* doc = dom_engine_load_document_native(p);
    if (!doc) return ItemNull;
    void* doc_node = dom_get_or_create_doc_node(doc);
    return doc_node ? dom_wrap_element(doc_node) : ItemNull;
}

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
// Property WRITES through the realm-shared protocol (D7.4.4). These were kept
// out of the Lambda face because they faulted with no JS realm (ESO81); the
// fault was the unguarded prototype fallback in the getter, not the write, so
// with that guarded they are ordinary neutral operations.
extern "C" Item dom_core_set_text_content(Item n, Item text) {
    return dom_set_property_impl(n, js_name_item("textContent"), text);
}
extern "C" Item dom_core_set_inner_html(Item n, Item html) {
    return dom_set_property_impl(n, js_name_item("innerHTML"), html);
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

// --- the generic entries (ES45)
//
// A binding layer needs to reach an operation it only knows by name or ordinal:
// `el[name]` is dynamic, so JS cannot use a fixed row per property. These three
// are that generic path, and making them rows is what puts the *whole* of JS's
// DOM access behind the API rather than most of it.
//
// get_property and set_property already have the catalog's shape. The ordinal
// executor does not -- it takes an enum and a C array -- so the row spells the
// arguments as a Lambda array and unpacks them here, which keeps every row
// fixed-arity and leaves the variadic form where it belongs, behind the API.
// These pass absence through exactly as the protocol answers it. The rest of
// the catalog normalises JS `undefined` to null because Lambda has no undefined
// (ESO98) -- but that rule belongs at the *Lambda face*, and these three are the
// shared binding path both realms dispatch through. Normalising here turned
// every absent property into null for JS as well, which is a different value,
// and took two thirds of the DOM UI fixtures with it.
extern "C" Item dom_core_get_property(Item n, Item name) {
    return dom_get_property_impl(n, name);
}

extern "C" Item dom_core_set_property(Item n, Item name, Item value) {
    return dom_set_property_impl(n, name, value);
}

extern "C" Item dom_core_invoke(Item n, Item op, Item args) {
    if (get_type_id(op) != LMD_TYPE_INT) return ItemNull;
    JubeDomElementOperation operation = (JubeDomElementOperation)it2i(op);
    int64_t argc = get_type_id(args) == LMD_TYPE_ARRAY ? js_array_length(args) : 0;
    if (argc <= 0) return dom_element_operation_impl(n, operation, nullptr, 0);
    // The executor allocates while it reads this span, so the unpacked arguments
    // live in a root region, not a native buffer (D5.2.1). The region is sized to
    // argc and fails closed on reservation overflow, so no arity cap is needed.
    RootSpan roots((size_t)argc);
    Item* unpacked = roots.items();
    if (!unpacked) return ItemError;
    for (int64_t k = 0; k < argc; k++) unpacked[k] = js_elements_get_int(args, k);
    return dom_element_operation_impl(n, operation, unpacked, (int)argc);
}

// --- CSS statics and the two node writes JS reached for directly (ES45)
//
// These four were the DOM operations the JS runtime called by direct extern
// rather than through the API. Each gets a row in the catalog's uniform shape
// so JS crosses the same entry point every other caller does; the bodies below
// are adapters, not new mechanism.
extern "C" Item dom_dispatch_event_bridge(Item target, Item event);
extern "C" Item dom_css_supports_operation(Item* args, int argc);
extern "C" Item dom_css_escape_operation(Item* args, int argc);
extern "C" Item dom_dataset_set_property(Item elem_item, Item prop_name, Item value);
extern "C" void dom_event_handler_property_set(Item target, const char* property_name,
                                                int property_name_len, Item value);

// CSS.supports is overloaded in the spec -- supports(property, value) and
// supports(conditionText) -- which is why the body underneath is variadic. The
// row is fixed at two arguments and the one-argument spelling passes null,
// keeping every row in the catalog the same shape.
extern "C" Item dom_core_css_supports(Item property, Item value) {
    Item args[2] = { property, value };
    int argc = get_type_id(value) == LMD_TYPE_NULL ? 1 : 2;
    return dom_absent_to_null(dom_css_supports_operation(args, argc));
}

extern "C" Item dom_core_css_escape(Item text) {
    Item args[1] = { text };
    return dom_absent_to_null(dom_css_escape_operation(args, 1));
}

// element.dataset.foo = v is a data-* attribute write. The row takes the
// element, not the dataset proxy: unwrapping the proxy is a JS-object concern
// and stays on the JS side, where the proxy exists.
extern "C" Item dom_core_set_data(Item n, Item name, Item value) {
    return dom_absent_to_null(dom_dataset_set_property(n, name, value));
}

// el.onclick = fn. This shares the listener store add_listener and
// remove_listener use -- it finds the node's listeners, tombstones the previous
// IDL handler and installs the new one -- so it is a derived operation in
// substance. Stating it as a derivation needs a read for the current IDL
// handler, which no row exposes yet; until then the row wraps the existing body
// and the derivation string records what it would be.
extern "C" Item dom_core_set_event_handler(Item n, Item name, Item handler) {
    const char* prop = dom_cstr_or_null(name);
    if (!prop) return ItemNull;
    dom_event_handler_property_set(n, prop, (int)strlen(prop), handler);
    return ItemNull;
}

// --- events
// `dispatch` takes either spelling: a bare type name, which is how the engine's
// script dispatch has always been called, or an event value carrying its own
// flags. The catalog types the second argument as an event and the engine took
// a name, and that gap is why `click` -- whose derivation dispatches a
// cancelable, bubbling, composed event -- could not be expressed at all.
//
// An event value is an ordinary Lambda map: {type, bubbles, cancelable}. There
// is deliberately no constructor for it. Lambda has map literals, and a native
// factory for something the language already writes would be one more body to
// keep in step (`create_event` remains what it has always been: JS's legacy
// document.createEvent, which returns a JS object and so needs the realm).
// Read a field of an event value. The event is an ordinary Lambda map, so this
// reads it as one: dom_realm_get_cstr builds its key through the JS realm's
// allocator and faults on a realm-less document, which is the whole reason the
// map spelling exists.
static Item dom_map_field(Item map_item, const char* key) {
    if (get_type_id(map_item) != LMD_TYPE_MAP || !map_item.map) return ItemNull;
    return dom_absent_to_null(map_get(map_item.map, js_name_item(key)));
}

static bool dom_event_flag(Item event, const char* key, bool fallback) {
    Item v = dom_map_field(event, key);
    return get_type_id(v) == LMD_TYPE_BOOL ? it2b(v) : fallback;
}

// The live-event predicate is the module's: an event is a record-backed
// wrapper, and only the module can say whether a wrapper is one of its own.
extern "C" bool radiant_dom_event_is(Item item);
extern "C" Item radiant_dom_event_create(const char* type, bool bubbles,
                                         bool cancelable, bool composed, int class_id);

extern "C" Item dom_core_dispatch(Item n, Item event) {
    // A live event goes straight through: it already carries the propagation
    // state a listener mutates, and dom_dispatch_event's F19/ES25 bridge enters
    // the engine's cascade with it.
    if (radiant_dom_event_is(event)) {
        return dom_absent_to_null(dom_dispatch_event_bridge(n, event));
    }
    // A descriptor -- a name, or {type, bubbles, cancelable} -- says how to
    // build one. Build it, then dispatch it, so there is a single dispatch
    // implementation rather than two that disagree.
    //
    // The engine's own descriptor entry is *not* that implementation: it fails
    // closed unless an EventContext is live ("only callable from a handler")
    // and takes a DomElement, so `dom.dispatch` answered false from a plain
    // script and could never target a document or window (ESO109). Going
    // through a real event instead keeps the re-entrant case working -- the
    // bridge above joins the cascade in progress -- and makes the top-level
    // case work for the first time. The factory is native (a VMap over a
    // RadiantDomEventRecord), so no realm is needed to build or read one.
    const char* type = nullptr;
    bool bubbles = true, cancelable = false;
    if (get_type_id(event) == LMD_TYPE_STRING) {
        type = fn_to_cstr(event);
    } else {
        Item type_item = dom_map_field(event, "type");
        if (get_type_id(type_item) != LMD_TYPE_STRING) return ItemNull;
        type = fn_to_cstr(type_item);
        bubbles = dom_event_flag(event, "bubbles", true);
        cancelable = dom_event_flag(event, "cancelable", false);
    }
    if (!type || !type[0]) return ItemNull;
    if (dom_engine_event_cascade_active()) {
        // Inside a handler the engine's entry is the right implementation: it
        // continues the cascade in progress, so an `input` raised while handling
        // a keystroke joins that walk instead of starting a second one. Routing
        // this case through the general path failed nine behaviour fixtures.
        Item type_item = { .item = 0 };
        type_item = js_make_string(type);
        Item b = { .item = b2it(bubbles) }, c = { .item = b2it(cancelable) };
        return dom_engine_dispatch_event(n, type_item, b, c);
    }
    Item built = radiant_dom_event_create(type, bubbles, cancelable, false, 0);
    if (get_type_id(built) != LMD_TYPE_VMAP) return ItemNull;
    return dom_absent_to_null(dom_dispatch_event_bridge(n, built));
}

// --- text controls
// These two bodies already existed in the core; only the catalog's uniform
// shape was missing, so each is a two-line adapter rather than an engine seam.
// tc_set_selection carries the direction the DOM's setSelectionRange takes --
// the radiant.* spelling had only three arguments, which is why this row could
// not simply forward to it.
extern "C" Item dom_text_control_set_selection_range_bridge(void* dom_elem, Item start_arg,
                                                            Item end_arg, Item dir_arg);
extern "C" Item dom_text_control_set_range_text_bridge(void* dom_elem, Item replacement_arg,
                                                        Item start_arg, Item end_arg, Item mode_arg);

extern "C" Item dom_core_tc_set_selection(Item n, Item start, Item end, Item dir) {
    void* elem = dom_unwrap_element(n);
    if (!elem) return ItemNull;
    return dom_absent_to_null(
        dom_text_control_set_selection_range_bridge(elem, start, end, dir));
}

extern "C" Item dom_core_tc_replace_range(Item n, Item start, Item end, Item text) {
    void* elem = dom_unwrap_element(n);
    if (!elem) return ItemNull;
    // "preserve" is setRangeText's default selection mode: replacing a range
    // must not move a caret the caller did not ask to move.
    return dom_absent_to_null(dom_text_control_set_range_text_bridge(
        elem, text, start, end, js_name_item("preserve")));
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
    // Same reasoning as dom_make_rect: a keyed result object needs an
    // allocator, and a Lambda-only document has no JS realm to borrow one
    // from, so build the pair out of the node's own document (ESO81).
    DomNode* node = (DomNode*)dom_unwrap_element(n);
    DomDocument* doc = (node && node->is_element()) ? ((DomElement*)node)->doc : nullptr;
    if (!dom_realm_active() && doc && doc->input) {
        MarkBuilder builder(doc->input);
        return builder.map()
            .put("x", dom_prop_get(n, "scrollLeft"))
            .put("y", dom_prop_get(n, "scrollTop"))
            .final();
    }
    Item out = js_new_object();
    dom_realm_set_cstr(out, "x", dom_prop_get(n, "scrollLeft"));
    dom_realm_set_cstr(out, "y", dom_prop_get(n, "scrollTop"));
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
    dom_realm_set_cstr(out, "start_container", js_range_get_start_container(r));
    dom_realm_set_cstr(out, "start_offset", js_range_get_start_offset(r));
    dom_realm_set_cstr(out, "end_container", js_range_get_end_container(r));
    dom_realm_set_cstr(out, "end_offset", js_range_get_end_offset(r));
    return out;
}
extern "C" Item dom_core_selection_boundaries(Item s) {
    Item out = js_new_object();
    dom_realm_set_cstr(out, "anchor_node", js_selection_get_anchor_node(s));
    dom_realm_set_cstr(out, "anchor_offset", js_selection_get_anchor_offset(s));
    dom_realm_set_cstr(out, "focus_node", js_selection_get_focus_node(s));
    dom_realm_set_cstr(out, "focus_offset", js_selection_get_focus_offset(s));
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
extern "C" Item dom_fp_document_element(Item n) {
    // The node's OWNING document's root element, read directly -- not the first
    // element child of a parent walk. The two agree for a connected node and
    // differ for a detached one, where the walk stops at the subtree's top and
    // answers the wrong element (it broke navigation and textarea handling
    // before the UI fixtures caught it).
    DomDocument* doc = (DomDocument*)dom_document_from_item(n);
    return (doc && doc->root) ? dom_wrap_element(doc->root) : ItemNull;
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
    // Its own derivation: the ordinal answers absence rather than `false` for a
    // node kind that has no attributes, and the row's contract is bool.
    Item value = dom_core_get_attribute(n, name);
    return (Item){.item = b2it(get_type_id(value) != LMD_TYPE_NULL)};
}
extern "C" Item dom_fp_inner_html(Item n) { return dom_prop_get(n, "innerHTML"); }
extern "C" Item dom_fp_outer_html(Item n) { return dom_prop_get(n, "outerHTML"); }
