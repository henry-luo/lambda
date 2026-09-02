#pragma once

/**
 * dom_realm_hooks.h — the core callbacks a JS realm installer publishes.
 *
 * ES33 splits *what a DOM object is* from *how a realm exposes it*. These three
 * build DOM objects and therefore stay in the core; the realm installer in
 * lambda/js/js_dom_realm.cpp publishes them as `DocumentFragment`,
 * `XPathEvaluator` and `Option` constructors. Keeping them behind one small
 * header is what lets the installers leave without dragging the DOM algorithms
 * (XPath matching, option selectedness) out with them.
 */

#include "../lambda.h"

/** `new DocumentFragment()` — a detached fragment on the current document. */
extern "C" Item dom_document_fragment_ctor(void);

/** `new XPathEvaluator()` — the evaluator object with its methods bound. */
extern "C" Item dom_xpath_evaluator_ctor(void);

/** `new Option(text, value, defaultSelected, selected)` — an <option> element. */
extern "C" Item dom_option_ctor(Item text_arg, Item value_arg,
                                Item def_sel_arg, Item sel_arg);

/** `new DOMMatrix()` / `new DOMPoint()` — geometry values the realm exposes. */
extern "C" Item dom_matrix_constructor(Item init);
extern "C" Item dom_point_constructor(Item x, Item y, Item z, Item w);

/**
 * Body behind `Element.prototype.querySelector(All)`. Libraries call those
 * through the prototype rather than an instance, so the realm has to publish
 * them there; the operation itself is the core's ordinary ordinal dispatch.
 */
extern "C" Item dom_element_prototype_operation_body(Item callee, Item this_value,
                                                    Item* args, int argc,
                                                    uint64_t* result_home);

/** Body behind `Element.prototype.animate`. */
extern "C" Item dom_element_animate(Item keyframes_item, Item options_item);

/**
 * The HTML tag -> WebIDL constructor-name table, read out by index so the
 * realm installer can publish each interface without the table itself
 * leaving the core (it also drives per-element prototype selection there).
 */
extern "C" int dom_html_interface_count(void);
extern "C" const char* dom_html_interface_ctor_name(int index);

/** window.prompt() — dequeues a harness-seeded answer; core behaviour. */
extern "C" Item dom_window_prompt(Item message_item, Item default_item);

// Realm installers, implemented in lambda/js/js_dom_realm.cpp. The core calls
// these when it binds a document that has a browsing context; each is a no-op
// for a document with no JS realm.
extern "C" void dom_install_collection_globals(void);
extern "C" void dom_install_option_constructor(void);
extern "C" void dom_install_window_dialog_globals(void);
extern "C" void dom_install_window_computed_style_global(void);

/** The frame windows of the active document, as a fresh array. */
extern "C" Item dom_collect_frame_windows_array(void);

/** Automation hooks: `__lambda_testdriver_key`, `__lambda_set_editing_behavior`. */
extern "C" Item dom_testdriver_key(Item key_item, Item shift_item, Item ctrl_item, Item alt_item, Item meta_item);
extern "C" Item dom_set_editing_behavior(Item behavior_item);

extern "C" void dom_install_window_frames_global(void);
extern "C" void dom_install_testdriver_globals(void);

/** DOMParser / XMLSerializer constructor bodies and their published methods. */
extern "C" Item dom_parser_constructor(void);
extern "C" Item dom_parser_parse_from_string(Item markup_item, Item mime_item);
extern "C" Item dom_xml_serializer_constructor(void);
extern "C" Item dom_xml_serializer_serialize_to_string(Item node_item);

extern "C" void dom_install_dom_parser_global(void);
extern "C" void dom_install_xml_serializer_global(void);

/** window.getSelection() body — the Selection object is core. */
extern "C" Item dom_global_get_selection(void);

/** document.createRange() body, and the selectionchange flush the realm binds. */
extern "C" Item dom_create_range(void);
extern "C" Item dom_flush_selectionchange(Item this_val, Item* args, int argc);

extern "C" void dom_selection_install_globals(void);
