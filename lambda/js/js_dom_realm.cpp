/**
 * js_dom_realm.cpp — publishing the DOM's WebIDL surface into a JS realm
 * (ESO79 slice 2 / ES33).
 *
 * Everything here answers one question: what does a *JavaScript realm* need to
 * see so that `document.createDocumentFragment() instanceof DocumentFragment`,
 * `Node.ELEMENT_NODE`, and `new Option(...)` behave? That is JS object-model
 * shape -- constructors, prototypes, interface links, string tags -- and by
 * ES33 it belongs on the adapter side, not in the realm-neutral DOM core.
 *
 * It used to live in lambda/dom/dom.cpp, interleaved with the XPath evaluator
 * and the Web Animations surface, which are DOM algorithms and stayed behind.
 * That interleaving is why this was extracted function by function rather than
 * as a line range: the range would have dragged the algorithms along with it.
 *
 * The three constructor callbacks these installers publish -- document
 * fragment, XPath evaluator, Option -- remain in the core and are reached
 * through dom_realm_hooks.h, because *what they build* is a DOM object while
 * *where it is published* is realm shape.
 */

#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../dom/dom.h"
#include "../dom/dom_realm_hooks.h"
#include "js_runtime.h"
#include "js_props.h"
#include "../dom/dom_ops.h"
#include "js_runtime_state.hpp"
#include "../jube/jube_registry.h"

// Range/Selection wrappers are Jube host types; the realm publishes their
// prototypes, the module owns their identity.
extern "C" const void* radiant_dom_range_host_type(void);
extern "C" const void* radiant_dom_selection_host_type(void);
#include "../runtime/lambda-root-frame.hpp"
#include "../../lib/log.h"
#include <cstring>

JS_FORWARD_STATIC_ITEM(_coll_illegal_constructor, (Item /*first*/), js_throw_type_error, ("Illegal constructor"))

static void _set_iface_to_string_tag(Item proto, const char* name) {
    if (get_type_id(proto) != LMD_TYPE_MAP || !name) return;
    // WebIDL interface prototypes carry @@toStringTag; selector/tooltip
    // libraries use this brand to distinguish a DOM Element from plain data.
    js_set_key_default(proto, js_well_known_symbol_key(4), js_name_item(name));
}

static void _install_iface(Item global, const char* name) {
    RootFrame roots(4);
    Rooted<Item> global_root(roots, global);
    Rooted<Item> key_root(roots, js_name_item(name));
    Rooted<Item> ctor_root(roots,
        js_get_key_default(global_root.get(), key_root.get()));
    Rooted<Item> proto_root(roots, ItemNull);
    if (js_is_callable(ctor_root.get())) {
        proto_root.set(js_get_key_cstr(ctor_root.get(), "prototype"));
        _set_iface_to_string_tag(proto_root.get(), name);
        return;
    }
    // Interface constructors share one native illegal-constructor callback but
    // must not share a cached JsFunction: each owns a distinct `.prototype`.
    // WebIDL illegal constructors still have [[Construct]]; their body, rather
    // than a missing capability, supplies the required TypeError (D6.2.2v2).
    ctor_root.set(js_new_distinct_native_constructor(
        _coll_illegal_constructor));
    js_set_function_name(ctor_root.get(), key_root.get());
    proto_root.set(js_new_object());
    _set_iface_to_string_tag(proto_root.get(), name);
    js_set_key_cstr(proto_root.get(), "constructor", ctor_root.get());
    js_initialize_native_constructor_prototype(ctor_root.get(),
        proto_root.get());
    js_set_key_default(global_root.get(), key_root.get(), ctor_root.get());
}

template <typename Target>
static void dom_install_value_constructor(Item global, const char* name,
        Target target, bool set_string_tag) {
    JS_ROOTS(roots,
        global_root, global,
        ctor_root, js_new_distinct_native_constructor(target),
        proto_root, js_new_object());
    Item name_key = js_name_item(name);
    js_set_function_name(ctor_root.get(), name_key);
    if (set_string_tag) _set_iface_to_string_tag(proto_root.get(), name);
    js_set_key_cstr(proto_root.get(), "constructor", ctor_root.get());
    js_initialize_native_constructor_prototype(ctor_root.get(),
        proto_root.get());
    js_set_key_default(global_root.get(), name_key, ctor_root.get());
}

JS_FORWARD_STATIC_VOID( _install_document_fragment_iface, (Item global), dom_install_value_constructor, (global, "DocumentFragment", dom_document_fragment_ctor, true))

static Item _iface_proto(Item global, const char* name) {
    Item ctor = js_get_key_default(global, js_name_item(name));
    if (!js_is_callable(ctor)) return ItemNull;
    Item proto = js_get_key_cstr(ctor, "prototype");
    return get_type_id(proto) == LMD_TYPE_MAP ? proto : ItemNull;
}

static void _install_nodelist_for_each(Item global) {
    Item node_list_proto = _iface_proto(global, "NodeList");
    Item array_ctor = js_get_key_cstr(global, "Array");
    Item array_proto = js_get_key_cstr(array_ctor, "prototype");
    Item array_for_each = js_get_key_cstr(array_proto, "forEach");
    if (get_type_id(node_list_proto) == LMD_TYPE_MAP &&
        js_is_callable(array_for_each)) {
        // Query APIs return Arrays, but libraries feature-detect the WebIDL
        // NodeList prototype before choosing their iteration path.
        js_set_key_cstr(node_list_proto, "forEach", array_for_each);
    }
}

static void _link_iface_proto(Item global, const char* name, const char* base_name) {
    Item proto = _iface_proto(global, name);
    Item base_proto = _iface_proto(global, base_name);
    if (get_type_id(proto) == LMD_TYPE_MAP && get_type_id(base_proto) == LMD_TYPE_MAP) {
        js_set_prototype(proto, base_proto);
    }
}

static void _set_ctor_int_constant(Item ctor, const char* name, int64_t value) {
    js_set_key_default(ctor, js_name_item(name),
        (Item){.item = i2it(value)});
}

JS_FORWARD_STATIC_VOID( _install_xpath_evaluator, (Item global), dom_install_value_constructor, (global, "XPathEvaluator", dom_xpath_evaluator_ctor, false))

static void _install_node_iface(Item global) {
    RootFrame roots(3);
    Rooted<Item> global_root(roots, global);
    Rooted<Item> ctor_root(roots,
        js_new_distinct_native_constructor(_coll_illegal_constructor));
    Rooted<Item> proto_root(roots, js_new_object());
    js_set_function_name(ctor_root.get(), js_name_item("Node"));
    _set_iface_to_string_tag(proto_root.get(), "Node");
    js_set_key_cstr(proto_root.get(), "constructor", ctor_root.get());
    js_initialize_native_constructor_prototype(ctor_root.get(),
        proto_root.get());
    static const struct { const char* name; int value; } node_constants[] = {
        {"ELEMENT_NODE", 1}, {"ATTRIBUTE_NODE", 2}, {"TEXT_NODE", 3},
        {"CDATA_SECTION_NODE", 4}, {"ENTITY_REFERENCE_NODE", 5},
        {"ENTITY_NODE", 6}, {"PROCESSING_INSTRUCTION_NODE", 7},
        {"COMMENT_NODE", 8}, {"DOCUMENT_NODE", 9},
        {"DOCUMENT_TYPE_NODE", 10}, {"DOCUMENT_FRAGMENT_NODE", 11},
        {"NOTATION_NODE", 12},
    };
    for (size_t i = 0; i < sizeof(node_constants) / sizeof(node_constants[0]); i++) {
        _set_ctor_int_constant(ctor_root.get(), node_constants[i].name,
            node_constants[i].value);
    }
    js_set_key_cstr(global_root.get(), "Node", ctor_root.get());
}

extern "C" void dom_install_collection_globals(void) {
    Item global = js_get_global_this();
    _install_iface(global, "Window");
    _link_iface_proto(global, "Window", "EventTarget");
    Item window_proto = _iface_proto(global, "Window");
    if (get_type_id(window_proto) == LMD_TYPE_MAP) {
        // The browsing-context global is the Window instance; exposing only
        // `window` left WebIDL brand checks such as event.view instanceof Window
        // unable to enter pointer-driven editor paths.
        js_set_prototype(global, window_proto);
    }
    _install_node_iface(global);
    // Document wrappers are module-owned, but bare WebIDL constructor lookup
    // must still succeed before libraries inspect static Document features.
    static const char* iface_links[][2] = {
        {"Document", "Node"}, {"HTMLDocument", "Document"},
        {"Element", "Node"}, {"HTMLElement", "Element"},
        {"SVGElement", "Element"}, {"SVGGraphicsElement", "SVGElement"},
        {"SVGSVGElement", "SVGGraphicsElement"},
        {"SVGPathElement", "SVGGraphicsElement"},
        {"SVGTextContentElement", "SVGGraphicsElement"},
    };
    for (size_t i = 0; i < sizeof(iface_links) / sizeof(iface_links[0]); i++) {
        _install_iface(global, iface_links[i][0]);
        _link_iface_proto(global, iface_links[i][0], iface_links[i][1]);
    }
    // JointJS Vectorizer gates its SVG implementation on window.SVGAngle;
    // without this legacy WebIDL interface it installs a non-SVG fallback.
    static const char* svg_ifaces[] = {"SVGAngle", "SVGMatrix", "SVGTransform", "SVGPoint"};
    for (size_t i = 0; i < sizeof(svg_ifaces) / sizeof(svg_ifaces[0]); i++) {
        _install_iface(global, svg_ifaces[i]);
    }
    dom_install_value_constructor(global, "DOMMatrix", dom_matrix_constructor, true);
    dom_install_value_constructor(global, "DOMPoint", dom_point_constructor, true);
    int html_interface_count = dom_html_interface_count();
    for (int i = 0; i < html_interface_count; i++) {
        // Specialized HTML wrappers must inherit HTMLElement so WebIDL brand
        // checks do not collapse every form control to the generic interface.
        const char* ctor_name = dom_html_interface_ctor_name(i);
        _install_iface(global, ctor_name);
        _link_iface_proto(global, ctor_name, "HTMLElement");
    }
    _install_document_fragment_iface(global);
    _link_iface_proto(global, "DocumentFragment", "Node");
    _install_iface(global, "ShadowRoot");
    _link_iface_proto(global, "ShadowRoot", "DocumentFragment");
    Item element_proto = _iface_proto(global, "Element");
    if (get_type_id(element_proto) == LMD_TYPE_MAP) {
        // Bootstrap deliberately calls these WebIDL methods through
        // Element.prototype.querySelector(All).call(element, selector).
        RootFrame method_roots(2);
        Rooted<Item> element_proto_root(method_roots, element_proto);
        Rooted<Item> method_root(method_roots,
            js_new_native_payload_function(dom_element_prototype_operation_body,
                (uint64_t)JUBE_DOM_QUERY_SELECTOR, 1));
        // Bootstrap needs these WebIDL prototype aliases before any instance
        // exists. Each carries its direct operation payload; populating every
        // Jube prototype here would mutate the sealed NameId module table.
        js_set_key_cstr(element_proto_root.get(), "querySelector", method_root.get());
        method_root.set(js_new_native_payload_function(
            dom_element_prototype_operation_body,
            (uint64_t)JUBE_DOM_QUERY_SELECTOR_ALL, 1));
        js_set_key_cstr(element_proto_root.get(), "querySelectorAll", method_root.get());
        js_set_native_key(element_proto_root.get(), js_name_item("animate"), dom_element_animate);
    }
    static const char* collection_ifaces[] = {
        "Range", "Selection", "HTMLCollection", "HTMLFormControlsCollection",
        "HTMLOptionsCollection", "NodeList",
    };
    for (size_t i = 0; i < sizeof(collection_ifaces) / sizeof(collection_ifaces[0]); i++) {
        _install_iface(global, collection_ifaces[i]);
    }
    _install_iface(global, "CSSNestedDeclarations");
    _install_nodelist_for_each(global);
    _install_iface(global, "RadioNodeList");
    _install_xpath_evaluator(global);
    log_debug("dom_install_collection_globals: installed collection interfaces");
}

extern "C" void dom_install_option_constructor(void) {
    Item global = js_get_global_this();
    Item ctor = js_new_native_constructor(dom_option_ctor);
    js_set_function_name(ctor, js_name_item("Option"));
    Item proto = js_new_object();
    js_set_key_cstr(proto, "constructor", ctor);
    js_set_key_cstr(ctor, "prototype", proto);
    js_set_key_cstr(global, "Option", ctor);
    log_debug("dom_install_option_constructor: installed Option");
}


// ---------------------------------------------------------------------------
// Window-level publications
// ---------------------------------------------------------------------------

JS_FORWARD_STATIC_ITEM(js_window_get_computed_style,
    (Item elem_item, Item pseudo_item), dom_get_computed_style,
    (elem_item, pseudo_item))

extern "C" void dom_install_window_computed_style_global(void) {
    Item global = js_get_global_this();
    Item key = js_name_item("getComputedStyle");
    Item existing = js_get_key_default(global, key);
    if (js_is_callable(existing)) return;
    Item fn = js_new_native_function(js_window_get_computed_style);
    js_set_function_name(fn, key);
    // getComputedStyle is a Window/global function now; direct MIR DOM shims
    // were removed so calls resolve through ordinary property dispatch.
    js_set_key_default(global, key, fn);
}

extern "C" void dom_install_window_dialog_globals(void) {
    // The queue this answers from is core state; only the publication is realm
    // shape, so the two live on opposite sides of the seam.
    Item fn = js_new_native_function(dom_window_prompt);
    Item global = js_get_global_this();
    js_set_key_cstr(global, "prompt", fn);
    Item window = js_get_key_cstr(global, "window");
    if (get_type_id(window) == LMD_TYPE_MAP) {
        js_set_key_cstr(window, "prompt", fn);
    }
}

extern "C" void dom_install_window_frames_global(void) {
    // The core answered "which frames"; this decides where a realm sees them.
    Item frames = dom_collect_frame_windows_array();
    Item length_item = (Item){.item = i2it(js_array_length(frames))};
    Item global = js_get_global_this();
    js_set_key_cstr(global, "frames", frames);
    js_set_key_cstr(global, "length", length_item);

    Item window = js_get_key_cstr(global, "window");
    if (get_type_id(window) == LMD_TYPE_MAP) {
        js_set_key_cstr(window, "frames", frames);
        js_set_key_cstr(window, "length", length_item);
    }
}

// The automation harness reaches the document through two global hooks. Their
// bodies are core behaviour; only the names they answer to are realm shape.
extern "C" void dom_install_testdriver_globals(void) {
    Item global = js_get_global_this();
    if (get_type_id(global) != LMD_TYPE_MAP) return;
    js_set_native_key(global, js_name_item("__lambda_testdriver_key"),
        dom_testdriver_key);
    js_set_native_key(global, js_name_item("__lambda_set_editing_behavior"),
        dom_set_editing_behavior);
}

// Constructor-shaped globals: DOMParser and XMLSerializer.
template <typename Method>
static void dom_install_native_constructor_global(const char* ctor_name,
        JsNativeP0 ctor_target, const char* method_name, Method method_target) {
    JS_ROOTS(roots,
        global_root, js_get_global_this(),
        ctor_root, js_new_native_constructor(ctor_target),
        proto_root, js_new_object(),
        method_root, js_new_native_function(method_target));
    js_set_function_name(ctor_root.get(), js_name_item(ctor_name));
    js_set_key_cstr(proto_root.get(), "constructor", ctor_root.get());
    js_set_key_default(proto_root.get(), js_name_item(method_name),
        method_root.get());
    js_initialize_native_constructor_prototype(ctor_root.get(),
        proto_root.get());
    js_set_key_default(global_root.get(), js_name_item(ctor_name),
        ctor_root.get());
}

extern "C" void dom_install_dom_parser_global(void) {
    // D6.2.2v2: the prototype owns the parse capability, so construction and
    // method publication share one rooted native-constructor transaction.
    dom_install_native_constructor_global("DOMParser", dom_parser_constructor,
        "parseFromString", dom_parser_parse_from_string);
}

JS_FORWARD_VOID( dom_install_xml_serializer_global, (void), dom_install_native_constructor_global, ("XMLSerializer", dom_xml_serializer_constructor, "serializeToString", dom_xml_serializer_serialize_to_string))

// window.getSelection() and the window/document self-references it needs.
// The Selection object itself is core; this only decides where a realm sees it.
extern "C" void dom_selection_install_globals(void) {
    Item global = js_get_global_this();
    Item fn = js_new_native_function(dom_global_get_selection);
    js_set_key_cstr(global, "getSelection", fn);
    // Ensure `window` resolves to globalThis so `window.getSelection()` works.
    Item window_key = js_name_item("window");
    Item existing = js_get_key_default(global, window_key);
    if (get_type_id(existing) != LMD_TYPE_MAP) {
        js_set_key_default(global, window_key, global);
    } else {
        // window is already a real object — install getSelection on it too
        js_set_key_cstr(existing, "getSelection", fn);
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
    js_set_key_cstr(global, "document", doc_proxy);
    if (get_type_id(existing) == LMD_TYPE_MAP)
        js_set_key_cstr(existing, "document", doc_proxy);

    Item flush_fn = js_new_native_this_span_function(
        dom_flush_selectionchange);
    js_set_key_cstr(global, "__lambdaFlushSelectionChange", flush_fn);
    if (get_type_id(existing) == LMD_TYPE_MAP)
        js_set_key_cstr(existing, "__lambdaFlushSelectionChange", flush_fn);

    // Install placeholder Selection / Range constructors so `instanceof Selection`
    // and feature-detection (`window.Selection`) succeed. The constructors are
    // never actually invoked by typical WPT code (which uses document.createRange
    // / getSelection); identity comes from their function names plus DOM host
    // fast paths in js_instanceof_classname.
    Item sel_ctor   = js_new_native_function(dom_global_get_selection);
    Item range_ctor = js_new_native_constructor(dom_create_range);
    js_set_function_name(sel_ctor, js_name_item("Selection"));
    js_set_function_name(range_ctor, js_name_item("Range"));
    js_set_key_cstr(global, "Selection", sel_ctor);
    js_set_key_cstr(global, "Range", range_ctor);

    // Install Selection.prototype and Range.prototype with method stubs so
    // WPT idl checks like `Selection.prototype.deleteFromDocument.length`
    // succeed. The methods themselves are never invoked through the
    // prototype path (instances are DOM resources and dispatch through their
    // own get_property hooks); these are pure idl shape.
    Item sel_proto = js_get_key_cstr(sel_ctor, "prototype");
    if (get_type_id(sel_proto) != LMD_TYPE_MAP) {
        sel_proto = js_new_object();
        js_set_key_cstr(sel_ctor, "prototype", sel_proto);
    }
    Item range_proto = js_get_key_cstr(range_ctor, "prototype");
    if (get_type_id(range_proto) != LMD_TYPE_MAP) {
        range_proto = js_new_object();
        js_set_key_cstr(range_ctor, "prototype", range_proto);
    }
    // DOM3: force the jube type prototypes now that the ctors' .prototype
    // objects exist — this publishes the declared method function objects onto
    // Range.prototype / Selection.prototype (IDL shape, .length probes) before
    // any script can read them.
    jube_type_prototype((const JubeTypeDef*)radiant_dom_range_host_type());
    jube_type_prototype((const JubeTypeDef*)radiant_dom_selection_host_type());

    // Set document.defaultView = window so DOM tests' sanity checks pass.
    Item doc = js_get_document_object_value();
    dom_document_proxy_set_property(js_name_item("defaultView"), global);
    (void)doc;

    log_debug("dom_selection: installed global getSelection / Selection / Range");
}
