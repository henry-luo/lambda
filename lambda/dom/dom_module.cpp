/**
 * dom_module.cpp — the Lambda-facing face of the DOM core (`import dom`).
 *
 * ES36. Page JavaScript reaches the DOM core through the Jube declared
 * interfaces and the JubeHostDomAPI seam; this module is the same core's other
 * door, for Lambda scripts. Every function here is a thin delegation to
 * dom_ops.h -- the ordinal executor and the two property entries -- so a `dom.*`
 * call and the equivalent JS call land on one implementation (ES38). Nothing in
 * this file implements DOM behaviour of its own.
 *
 * Collections are returned as ordinary Lambda arrays, i.e. snapshots (S9.2.2):
 * a Lambda caller iterating the result of dom.query_all() walks the value it
 * was handed, and mutating the tree mid-iteration does not perturb it. Live
 * collections stay a JS-adapter concern (DOM_Pkg Q4).
 *
 * Document acquisition is deliberately absent -- see ESO80. Creating a document
 * needs Radiant's loader, which lives in the radiant link target, above this
 * one; a Lambda script obtains a root through `radiant.load(path)` today and
 * drives it with `dom.*` from there.
 */

#include "../lambda-data.hpp"
#include "dom.h"
#include "dom_ops.h"
#include "../js/js_runtime.h"
#include "../jube/jube_registry.h"
#include "../../lib/log.h"

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

/** Read a DOM IDL property by its spec (camelCase) name. */
static Item dom_prop_get(Item node, const char* name) {
    return dom_get_property_impl(node, js_name_item(name));
}

// ---------------------------------------------------------------------------
// Tree reads
// ---------------------------------------------------------------------------

extern "C" Item fn_dom_root(Item node) {
    return dom_op0(node, JUBE_DOM_GET_ROOT_NODE);
}

extern "C" Item fn_dom_tag(Item node) {
    return dom_prop_get(node, "tagName");
}

extern "C" Item fn_dom_text(Item node) {
    return dom_prop_get(node, "textContent");
}

extern "C" Item fn_dom_parent(Item node) {
    return dom_prop_get(node, "parentNode");
}

extern "C" Item fn_dom_first_element_child(Item node) {
    return dom_prop_get(node, "firstElementChild");
}

extern "C" Item fn_dom_next_element_sibling(Item node) {
    return dom_prop_get(node, "nextElementSibling");
}

// ---------------------------------------------------------------------------
// Selector queries -- the same matcher the layout engine and JS queries use
// ---------------------------------------------------------------------------

extern "C" Item fn_dom_query(Item node, Item selector) {
    return dom_op1(node, JUBE_DOM_QUERY_SELECTOR, selector);
}

extern "C" Item fn_dom_query_all(Item node, Item selector) {
    return dom_op1(node, JUBE_DOM_QUERY_SELECTOR_ALL, selector);
}

extern "C" Item fn_dom_matches(Item node, Item selector) {
    return dom_op1(node, JUBE_DOM_MATCHES, selector);
}

extern "C" Item fn_dom_closest(Item node, Item selector) {
    return dom_op1(node, JUBE_DOM_CLOSEST, selector);
}

// getElementsByTagName / -ClassName are deliberately not exposed: they return
// *live* HTMLCollections, whose refresh machinery is JS-realm state, and S9.2.2
// gives Lambda snapshot semantics anyway. query_all() is the snapshot spelling
// of the same query and is what a Lambda caller should reach for.

extern "C" Item fn_dom_element_by_id(Item node, Item id) {
    return dom_op1(node, JUBE_DOM_GET_ELEMENT_BY_ID, id);
}

// ---------------------------------------------------------------------------
// Attributes
// ---------------------------------------------------------------------------

extern "C" Item fn_dom_attr(Item node, Item name) {
    return dom_op1(node, JUBE_DOM_GET_ATTRIBUTE, name);
}

extern "C" Item fn_dom_set_attr(Item node, Item name, Item value) {
    return dom_op2(node, JUBE_DOM_SET_ATTRIBUTE, name, value);
}

extern "C" Item fn_dom_has_attr(Item node, Item name) {
    return dom_op1(node, JUBE_DOM_HAS_ATTRIBUTE, name);
}

extern "C" Item fn_dom_remove_attr(Item node, Item name) {
    return dom_op1(node, JUBE_DOM_REMOVE_ATTRIBUTE, name);
}

// ---------------------------------------------------------------------------
// Mutation
// ---------------------------------------------------------------------------

extern "C" Item fn_dom_append(Item parent, Item child) {
    return dom_op1(parent, JUBE_DOM_APPEND_CHILD, child);
}

extern "C" Item fn_dom_insert_before(Item parent, Item child, Item ref) {
    return dom_op2(parent, JUBE_DOM_INSERT_BEFORE, child, ref);
}

extern "C" Item fn_dom_remove_child(Item parent, Item child) {
    return dom_op1(parent, JUBE_DOM_REMOVE_CHILD, child);
}

extern "C" Item fn_dom_remove(Item node) {
    return dom_op0(node, JUBE_DOM_REMOVE);
}

extern "C" Item fn_dom_clone(Item node, Item deep) {
    return dom_op1(node, JUBE_DOM_CLONE_NODE, deep);
}

extern "C" Item fn_dom_contains(Item node, Item other) {
    return dom_op1(node, JUBE_DOM_CONTAINS, other);
}

// Property *writes* (textContent, innerHTML) are deliberately absent: they
// reach JS intrinsic-constructor creation and segfault without a live JS realm,
// which a Lambda-only script does not have. The ordinal executor above has no
// such dependency, so attribute and tree mutation work. See ESO81.

// ---------------------------------------------------------------------------
// Serialization -- parse and serialize run through Radiant's HTML machinery,
// so what a Lambda script writes is what the layout engine reads back.
// ---------------------------------------------------------------------------

extern "C" Item fn_dom_inner_html(Item node) {
    return dom_prop_get(node, "innerHTML");
}

extern "C" Item fn_dom_outer_html(Item node) {
    return dom_prop_get(node, "outerHTML");
}

// ---------------------------------------------------------------------------
// Module descriptor
//
// Node-shaped parameters and results are declared `any` rather than `dom_node`:
// the branded wrapper type is registered by the radiant module, and a Jube
// signature resolves type names against its own module's table. The *values*
// are the same branded wrappers either door produces, so member access on them
// behaves identically -- only the static spelling is looser. Tightening it
// needs cross-module type references (ESO80).
// ---------------------------------------------------------------------------

#define DOM_FN(lname, sig, impl) \
    { lname, sig, (fn_ptr)impl, JUBE_FN_NONE, nullptr, (fn_ptr)impl }

static const JubeFuncDef dom_functions[] = {
    // tree reads
    DOM_FN("root", "fn(node: any) -> any", fn_dom_root),
    DOM_FN("tag", "fn(node: any) -> any", fn_dom_tag),
    DOM_FN("text", "fn(node: any) -> any", fn_dom_text),
    DOM_FN("parent", "fn(node: any) -> any", fn_dom_parent),
    DOM_FN("first_element_child", "fn(node: any) -> any", fn_dom_first_element_child),
    DOM_FN("next_element_sibling", "fn(node: any) -> any", fn_dom_next_element_sibling),
    // queries
    DOM_FN("query", "fn(node: any, selector: string) -> any", fn_dom_query),
    DOM_FN("query_all", "fn(node: any, selector: string) -> any", fn_dom_query_all),
    DOM_FN("matches", "fn(node: any, selector: string) -> any", fn_dom_matches),
    DOM_FN("closest", "fn(node: any, selector: string) -> any", fn_dom_closest),
    DOM_FN("element_by_id", "fn(node: any, id: string) -> any", fn_dom_element_by_id),
    // attributes
    DOM_FN("attr", "fn(node: any, name: string) -> any", fn_dom_attr),
    DOM_FN("set_attr", "fn(node: any, name: string, value: any) -> any", fn_dom_set_attr),
    DOM_FN("has_attr", "fn(node: any, name: string) -> any", fn_dom_has_attr),
    DOM_FN("remove_attr", "fn(node: any, name: string) -> any", fn_dom_remove_attr),
    // mutation
    DOM_FN("append", "fn(parent: any, child: any) -> any", fn_dom_append),
    DOM_FN("insert_before", "fn(parent: any, child: any, ref: any) -> any", fn_dom_insert_before),
    DOM_FN("remove_child", "fn(parent: any, child: any) -> any", fn_dom_remove_child),
    DOM_FN("remove", "fn(node: any) -> any", fn_dom_remove),
    DOM_FN("clone", "fn(node: any, deep: any) -> any", fn_dom_clone),
    DOM_FN("contains", "fn(node: any, other: any) -> any", fn_dom_contains),
    // serialization
    DOM_FN("inner_html", "fn(node: any) -> any", fn_dom_inner_html),
    DOM_FN("outer_html", "fn(node: any) -> any", fn_dom_outer_html),
};

#undef DOM_FN

// Zero is success here, matching every other Jube module's init.
static int dom_module_init(const JubeHostAPI* host) {
    // The module holds no state of its own: it forwards to the DOM core it is
    // linked with, so there is nothing to bind beyond the ABI check.
    if (!host || host->api_version != JUBE_HOST_API_VERSION) {
        log_error("JUBE_DOM: missing or mismatched host API during module init");
        return -1;
    }
    return 0;
}

static const JubeModuleDef dom_module = {
    JUBE_ABI_VERSION,
    sizeof(JubeModuleDef),
    "dom",
    "0.1.0",
    "DOM access for Lambda scripts over the shared DOM core",
    nullptr, 0,                                   // types: the wrappers are the
                                                  // radiant module's dom_node
    dom_functions,
    (int32_t)(sizeof(dom_functions) / sizeof(dom_functions[0])),
    nullptr, 0,                                   // namespaces
    dom_module_init,
    nullptr,                                      // shutdown
};

extern "C" void dom_jube_register_static(void) {
    jube_register_static_module(&dom_module);
}
